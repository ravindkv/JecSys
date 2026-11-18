# JecSys as a "mini combine" (draft 1, 2026-11-18)

A third design option, next to `refact.md` (layout, steps, config) and
`refactClass.md` (classes, naming). The idea is to organise the package the way
the Higgs combine tool
([HiggsAnalysis-CombinedLimit](https://github.com/cms-analysis/HiggsAnalysis-CombinedLimit/))
is organised: **datacard → workspace → fit methods**.

This is a discussion draft. Line numbers refer to today's files.

---

## 1. My take in short

**Adopt combine's architecture for the fit half of the package, and keep
`refact.md` for the input half.** They do not compete; combine's design only
covers what happens *after* the inputs exist.

| part of the chain | today | what the combine idea gives us |
|---|---|---|
| `reprocess`, `softrad3`, `globalFitSyst`, `globalFitRenormPF` (≈ 6350 lines) | builds `jecdata<epoch>.root` | **Nothing new.** In combine terms this is "datacard making", which combine leaves to the analysis (CombineHarvester etc.). `refact.md` §2–4 and the readers / steps of `refactClass.md` stay as they are. |
| `globalFitL3Res.C` lines 354–1616 (choose samples, load graphs, correct FSR, build sources) | hard-coded sample maps, bitmasks | **A datacard**: a text file that lists channels, nuisances and which nuisance acts on which channel. |
| lines 1940–2000 + `jesFitter` (χ²) + `jesFit` (model) | 103 globals, sample names inside the χ² | **A workspace + a physics model**: a frozen, self-contained fit problem and a generic engine with no sample or epoch names in it. |
| lines 2000–3000 (results, eigenvectors, 8 plots, text snippet) | one function | **Methods** (`-M FitDiagnostics`, `Impacts`, `GoodnessOfFit`, `MultiDimFit`, toys), each a small class that reads the same workspace. |

What I would **not** take from combine:

* **RooFit / RooWorkspace.** Our fit is a χ² with 9 parameters and ~27
  nuisances, not a Poisson likelihood. RooFit would add a heavy dependency
  and make exact parity with today's TMinuit `MINI` impossible. Our
  "workspace" is a plain ROOT file (§5). This is why it is a *mini* combine.
* **Processes, rates, signal strength.** We have no signal/background
  processes and no yields. A channel here is one response graph, not a
  histogram of event counts.
* **Python** (`text2workspace.py`, physics models in Python). Round 1 decided
  on C++ and one Makefile; all of the below is C++.
* **Running the real combine.** It needs CMSSW or a standalone build, and a
  custom RooFit pdf for the multijet and PF terms. It is far more work than
  the ~300 lines of χ² we have, and it gives no parity.

---

## 2. Why the analogy holds: our χ² is already a combine fit

`jesFitter` (line 3001) computes

```
χ² = Σ_channels Σ_points [ (data_i + Σ_s θ_s·src_s(pt_i) − model(pt_i; p)) / σ_i ]²
   + Σ_s θ_s²                         (one unit-Gaussian constraint per nuisance)
   + Σ_k p_k²                         (penalty on the 9 JES parameters)
   + PF-composition terms
```

That is a profile-likelihood fit with Gaussian-constrained nuisances, which is
what a combine datacard describes. The mapping:

| combine | JecSys today | mini combine |
|---|---|---|
| bin / channel | graph index `ig = isample + nsamples·imethod` | channel `ptchs_gamjet`, `mpfchs1_zjet`, … |
| `data_obs` shape | `_vdt1[ig]` | FSR-corrected response graph in the shapes file |
| `shape` nuisance | `TH1D` in `_vsrc`, target encoded as `bm<bitmask>_` in its name | a row in the card, one column per channel |
| `lnN` nuisance | flat histogram filled with `escale` (lines 1298–1388) | `flat` row with the number written in the card |
| POIs | `p[0..8]` of `jesFit` | parameters declared by the physics model |
| `PhysicsModel` | `jesFit` + `setToyShapeFuncs` + `_mpf` map | `JesModel9` class |
| `shapes` line | `Form("fsr/hkfsr3_%s_%s_%s", …)` etc. | one `shapes` line with `$METHOD`, `$SAMPLE` |
| `combineCards.py` | `globalFitRun2.C` (not ported) | `combineCards 2016BCDEF=… 2016GH=…` |
| `fitDiagnostics.root` | `sys/emat`, `hjesfit*`, `_vdt2` filled inside the χ² | `fit.root` written by `-M FitDiagnostics` |
| impacts, pulls, GoF | printouts | methods |

Three things in our χ² have no combine counterpart. They are handled by
giving each channel a **kind** (§4.3), not by bending the datacard:

1. **Multijet** (line 3041): the data point is multiplied by the model at a
   second pT (`ptref = crecoil·pt`), so the residual depends on the model
   twice.
2. **PF composition** (lines 3106–3262): prediction is
   `Σ_k 0.01·p_k·f_k(pt)` and the residual has a dead zone,
   `max(|data − pred| − δ, 0)/σ`. The same block is copied four times
   (dijet, Z, γ, combo).
3. **Closure reference**: the model divides by `_hjesref` (line 329), an
   input histogram, not a parameter.

---

## 3. Data flow

```
prefit/ + ${jercProtoLab}
   │   reprocess, jecBands, softRad, syst, (renormPf)      ← refact.md, unchanged
   ▼
forfit/<corLevel>/<epoch>/*.root
   │   makeCard          (was globalFitL3Res.C 354–1616)
   ▼
card/<corLevel>/<epoch>/datacard.txt + shapes.root         ← the contract (§4)
   │   combineCards      (optional: several epochs or η bins in one fit)
   │   text2workspace -P JesModel9
   ▼
card/<corLevel>/<epoch>/workspace.root                     ← frozen fit problem (§5)
   │   combine -M FitDiagnostics | Impacts | GoodnessOfFit | MultiDimFit | Toys
   ▼
postfit/<corLevel>/<epoch>/fit.root, result.json, impacts.json, ...
   │   plotFit / plotImpacts
   ▼
postfit/<corLevel>/<epoch>/plots/*.pdf
```

The important new boundary is `datacard.txt + shapes.root`. Everything left
of it is "analysis" (knows about input formats, epochs, FSR). Everything
right of it is "statistics" (knows only channels, nuisances and a model).

**`shapes.root` holds fit-ready objects.** Today the FSR correction
(`kfsr`, lines 1096–1180) and the scaling of the sources (`aeff`, lines
1255–1274) are applied in memory just before the fit. I propose `makeCard`
applies them and writes the corrected graphs. Then `text2workspace` is a
dumb, generic reader, and what was fitted can be inspected in a TBrowser.
Doubles survive a ROOT file round trip exactly, so parity is not at risk.

---

## 4. The datacard

### 4.1 Sketch for 2016GH, `MJDJ_gam_z_hadw`, `PtBalMPF`

Values and order are read off today's code (27 nuisances; I counted 16 FSR +
2 scale + 2 hdmscale + 4 hadW + 3 EES). Columns are abbreviated here.

```
# card/L1L2L3Res/2016GH/datacard.txt   written by makeCard, do not edit by hand
epoch     2016GH
corLevel  L1L2L3Res
etaBin    eta00-13
ptMin     15
----------------------------------------------------------------------------------------
shapes  data   *       shapes.root                              data/$CHANNEL
shapes  *      *       shapes.root                              nuisance/$NUISANCE
shapes  hadw_* *       ../2016BCDEF/shapes.root                 nuisance/$NUISANCE    # refIov
aux     crecoil  *_multijet  shapes.root                        aux/crecoil_$METHOD
aux     jesRef   *           shapes.root                        aux/herr_l2l3res
----------------------------------------------------------------------------------------
channel  ptchs_multijet ptchs_gamjet ptchs_zjet ptchs_hadw mpfchs1_multijet mpfchs1_gamjet mpfchs1_zjet mpfchs1_hadw
kind     multijet       response     response   response   multijet         response       response     response
----------------------------------------------------------------------------------------
channel  chf_pfjet nhf_pfjet nef_pfjet   chf_zjet nhf_zjet nef_zjet
kind     pfFraction pfFraction pfFraction pfFraction pfFraction pfFraction
delta    0.20      0.20      0.20        0.10     0.10     0.10
ptRange  43:600    43:600    43:600      25:300   25:300   25:300
----------------------------------------------------------------------------------------
# name                        type    ptMJ ptGJ ptZJ ptHW mpMJ mpGJ mpZJ mpHW
fsr_mpfu1_ptchs_multijet      shape   1    -    -    -    -    -    -    -
fsr_mpfu1_ptchs_gamjet        shape   -    1    -    -    -    -    -    -
...                                                              (16 FSR rows)
scale_gamjet                  flat    -    0.0020 -  -    -    0.0020 -  -
scale_zjet                    flat    -    -    0.0020 -  -    -    0.0020 -
hdmscale_gamjet               flat    -    0.0020 -  -    -    0.0020 -  -
hdmscale_zjet                 flat    -    -    0.0020 -  -    -    0.0020 -
hadw_ptave_fitprob            shape   -    -    -    1    -    -    -    -
hadw_ptboth_fitprob           shape   -    -    -    -    -    -    -    1
hadw_ptave_fitprob2           shape   -    -    -    1    -    -    -    -
hadw_ptboth_fitprob2          shape   -    -    -    -    -    -    -    1
eesfromzee_gamjet_eig0        shape   -    1    -    -    -    1    -    -
eesfromzee_gamjet_eig1        shape   -    1    -    -    -    1    -    -
eesfromzee_gamjet_eig2        shape   -    1    -    -    -    1    -    -
```

### 4.2 What this buys us immediately

* **The bitmask is gone.** `bm34_scale_020_gamjet` becomes a row with two
  filled columns. The 14 `nsample0map` / `igjmap` / `izllmap` tables (lines
  463–640) that exist only to compute bit positions disappear.
* **`toDo.txt` A4 is visible at a glance:** `scale_gamjet` and
  `hdmscale_gamjet` are the same row twice (same for `zjet`). A card
  validator can flag identical rows.
* **`toDo.txt` A3** (wrong bitmask) cannot happen: a target is a named
  column, not `1<<(n0+iwmh)`.
* **The refIOV dependency is one line** (`shapes hadw_* … ../2016BCDEF/…`)
  instead of three `TFile`s opened on `jecdata<refIOV>.root`.
* **Sample selection is data.** `"MJDJ_gam_z_hadw"` is no longer a magic
  string resolved in code; it is the list of columns. Dropping a channel for
  a cross-check is `combine --mask ptchs_hadw`, with no recompile and no new
  entry in a map.
* **Channel owners can check their own input.** The Z+jet, γ+jet and
  multijet inputs come from different groups; a card plus a shapes file is
  something they can read and validate without knowing our code.

### 4.3 Channel kinds

| kind | prediction | residual | extra input |
|---|---|---|---|
| `response` | `model(pt)` | `(data + shifts − pred)/σ` | — |
| `multijet` | `model(pt)` | data and σ multiplied by `model(crecoil·pt)` first | `aux crecoil` |
| `pfFraction` | `Σ_k 0.01·p_k·f_k^{frac}(pt)` | `max(\|data − pred\| − δ, 0)/σ` inside `ptRange` | `delta`, `ptRange` rows |

Three small classes behind one `Channel` interface replace the
`Contains("multijet")` test and the four copied PF blocks in `jesFitter`.
A future `dijet` kind (L2Res, scaled by the barrel JES) is a fourth class.

### 4.4 Format: text or JSON?

Round 1 decided on JSON for config. I still recommend the **combine-style
text table for the datacard only**:

* the nuisance × channel matrix is the content, and a table shows it; the
  same matrix as JSON is 27 objects with lists of names;
* everyone in JME already reads combine cards;
* it is generated by `makeCard`, not hand-edited, so the usual argument for
  JSON (validation of hand-written input) is weaker.

The settings that `makeCard` uses to *build* the card (which samples and
methods, flat values and their `_source`, α, `ptRecoMin`, FSR switches) stay
in `config/MakeCard.json`, with the era/epoch overrides of `refact.md` §4.
That file replaces `FitData.json`. The UL16-only values
(`escale = 0.0020` for γ+jet, line 1309) become an era override there.

If you prefer one format everywhere, the card can be JSON; nothing else in
this design changes.

---

## 5. The workspace

`text2workspace datacard.txt -P JesModel9 -o workspace.root` resolves every
`shapes` line, copies the objects into one file and adds the model:

```
workspace.root
  channels/<channel>/data          TGraphErrors
  channels/<channel>/kind, delta…  TNamed
  nuisances/<name>/<channel>       TH1D   (flat ones expanded to histograms)
  aux/…                            crecoil, jesRef
  model/name                       "JesModel9"
  model/config                     resolved JSON (parameters, start values, steps, penalties, shape constants)
  order/channels, order/nuisances  the order of the card, kept exactly
  _meta/                           card text, git hash, input paths and mtimes
```

Why have it as a file at all, when the card and shapes already define the fit?

* **It is frozen and self-contained.** One file can be copied, archived with
  a result, or sent to someone to reproduce a fit. Today that needs
  `jecdata<epoch>.root`, `jecdata<refIOV>.root` and the exact state of 103
  globals.
* **Every method reads the same thing.** `Impacts` and `GoodnessOfFit`
  cannot accidentally run on slightly different inputs than the nominal fit.
* **It is the unit of the regression test:** workspace in, numbers out.

**Order matters for parity.** MIGRAD's path depends on the parameter order.
`makeCard` must write the nuisances in today's order (FSR by eig × method ×
sample, scale, hdmscale, hadW, EES) and the engine must keep the card order.
The "inactive" all-zero FSR sources that today's code keeps (e.g. for hadW, lines
1220–1244) must stay as rows until parity is shown; dropping them is a
`toDo.txt` item.

---

## 6. Physics model

```cpp
// header/PhysicsModel.h
class PhysicsModel {
 public:
  virtual ~PhysicsModel() = default;
  virtual std::string name() const = 0;
  virtual const std::vector<FitParameter>& parameters() const = 0;   // name, start, step, range, penalty
  virtual double response(double pt, const double* par) const = 0;                      // was jesFit
  virtual double pfFraction(Fraction frac, double pt, const double* par) const = 0;     // was _mpf[sf][ipar]
};
```

* `JesModel9` is today's `njesFit==9` branch with the `DP_2021` shapes. Its
  numbers (shape constants, the `0.3` on `p5Frag`, `useP*` switches, start
  values `1,1,1,1,1,0,0,1,0`, step 0.01, range ±100) go to
  `config/JesModel9.json` with `_source` notes, as `refactClass.md` planned
  for `JesModel.json`.
* A model is chosen by name: `text2workspace -P JesModel9`. A small registry
  (`std::map<std::string, factory>`) replaces `if (njesFit==…)`. The "latest"
  shapes that sit in comments next to the `DP_2021` ones (e.g. line 3374)
  become `JesModelLatest` later, as a new class, without touching `JesModel9`.
* `response()` is a pure function: no `TF1::SetParameter` inside the χ²
  (`refactClass.md` §1), same numbers.

---

## 7. `combine`: methods and common options

One binary, one workspace in, results out. Each method is a class behind a
`FitMethod` interface, as in combine's `interface/` + `src/`.

| `-M` | what it does | replaces / new |
|---|---|---|
| `FitDiagnostics` | best fit, covariance, eigenvectors, χ² per channel, prefit and postfit graphs, nuisance pulls; writes `fit.root`, `result.json` | lines 1940–2600, `sys/emat`, `hjesfit*`, `fsr/hkfsr2_*`, the text snippet |
| `GoodnessOfFit` | χ²/ndf total and per channel; optional toys for a p-value | printout today; toys are new |
| `Impacts` | shift of the JES at chosen pT (and of each `p_k`) when one nuisance is fixed at ±1σ | new |
| `MultiDimFit` | χ² scan of one or two parameters | new |
| `GenerateOnly` / `-t N` | toy data from the postfit model, for bias and closure tests | new |

Common options (same names as combine where the meaning is the same):

```
combine workspace.root -M FitDiagnostics [-n <name>]
    --setParameters p5Frag=0,scale_gamjet=0      start values
    --freezeParameters p8Flv,fsr_.*              fix parameters (regex)
    --mask ptchs_hadw,.*_multijet                leave channels out of the χ²
    --ptMin 15  --ptMax 2000                     fit range
    --minimizer TMinuit|Minuit2                  TMinuit until parity is shown
    -t <N> --seed <s>                            toys
```

`--freezeParameters` and `--mask` replace most of the boolean globals at the
top of `globalFitL3Res.C` (`useP0Trk…useP8Flv`, `fitPF`, `fitPFZ`, the
choice of sample set). Each cross-check becomes a command line that can be
written in a log, instead of an edit that has to be remembered and reverted.

**The χ² no longer writes.** Today `jesFitter` fills `_vdt2`, `_vpt2` and
the PF output graphs as a side effect of every call. In this design
`FitDiagnostics` computes the shifted graphs once, from the best-fit
parameters, after the minimisation.

---

## 8. `combineCards`: several epochs or η bins in one fit

```
combineCards 2016BCDEF=card/L1L2L3Res/2016BCDEF/datacard.txt \
             2016GH=card/L1L2L3Res/2016GH/datacard.txt   > card/L1L2L3Res/Run2016/datacard.txt
```

Channels get the prefix (`2016GH_ptchs_gamjet`). As in combine, **nuisances
with the same name are correlated, different names are independent**, so
correlation between years is decided by naming in `MakeCard.json`
(`scale_gamjet` vs `scale_gamjet_2016GH`). The model option decides whether
the JES parameters are shared or per prefix.

This is the piece that makes the design worth its cost in the long run:

* the combined Run 2 fit (`globalFitRun2.C`, `toDo.txt` E2) becomes a card
  operation plus a model option, not a new 3000-line macro;
* a simultaneous fit can replace the sequential refIOV scheme (2016GH
  reading 2016BCDEF's fit result) if we want that later;
* L2Res-style fits in η bins are one card per η bin.

None of this is needed for parity with the four epochs. It should not be
built before Phase 5 passes.

---

## 9. Package layout

Combine's own layout (`interface/`, `src/`, `bin/`, `python/`, `scripts/`,
`data/`, `test/`, `docs/`) maps onto the round-1 decisions like this; I
keep your directory names.

```
JecSys/
├── Makefile
├── bin/                     one small main per executable (was main.cpp)
│   ├── runFit.cpp           driver: runs the whole chain for an epoch (as in refact.md §5)
│   ├── makeCard.cpp  combineCards.cpp  text2workspace.cpp  combine.cpp
│   └── plotFit.cpp  plotImpacts.cpp  compareRoot.cpp
├── header/  cpp/            flat, as decided
├── config/                  Inputs.json, <Class>.json, MakeCard.json, JesModel9.json
├── test/
├── prefit/  forfit/         as in refact.md
├── card/     (git-ignored)  card/<corLevel>/<epoch>/{datacard.txt, shapes.root, workspace.root}
├── postfit/  (git-ignored)  postfit/<corLevel>/<epoch>/{fit.root, result.json, impacts.json, plots/}
└── obj/
```

**Several executables or one?** Combine has several, and I recommend the
same: each stage can be run and tested alone, and the workspace is a real
hand-over point. `runFit` stays as the one command most people use
(`./runFit -e all`); it calls the same classes in sequence, it does not
shell out. `runFit -h` still lists the epochs from `Inputs.json`.

Classes that change relative to `refactClass.md` §4:

| `refactClass.md` | mini combine |
|---|---|
| `GlobalFitL3Res` (step) | `MakeCard` (step, writes card + shapes), then `Workspace`, then a `FitMethod` |
| `FitData` + `FitData.json` (explicit source list) | `Datacard` (parse, write, validate, combine) + `Workspace`; `MakeCard.json` |
| `JesModel` | `PhysicsModel` interface + `JesModel9` |
| `Chi2Function` | same, but loops over `Channel` objects; no sample names |
| `FitMinimizer`, `FitResult` | same |
| `FitPlotter` (step) | `plotFit`, reads only `fit.root` |
| — | `Channel` + `ResponseChannel`, `MultijetChannel`, `PfFractionChannel` |
| — | `FitMethod` + `FitDiagnostics`, `GoodnessOfFit`, `Impacts`, `MultiDimFit` |

This needs one change to design rule 5 of `refactClass.md` ("inheritance
only for `Step` and `ChannelReader`"): three more one-level interfaces,
`Channel`, `PhysicsModel` and `FitMethod`. All other rules, the camelCase
convention, `OutputFile`, `GraphKey`, `Inputs.json` and the provenance in
`_meta/` carry over unchanged.

---

## 10. Costs and risks

* **More moving parts for the same four fits.** A text format, a parser, a
  writer and a workspace layer, roughly 6–8 extra classes. If the package
  will only ever run the DP_2021 configuration for four epochs, the simpler
  `FitData.json` design of `refactClass.md` is enough, and this is
  over-engineering. It pays off if we expect any of: combined fits, new
  models, routine cross-checks (masking, freezing, impacts), other epochs.
* **Two descriptions of the fit inputs** (`MakeCard.json` → `datacard.txt`).
  The card must be generated, never the place where numbers are edited, or
  the two drift apart. I would have `makeCard` write a "do not edit" header
  and the `_source` of each flat value as a comment.
* **Parity is sensitive to order and to where corrections are applied**
  (§5). The test for this stage is strict: same χ², same 36 parameters and
  same `emat` as today, compared by `compareRoot`.
* **The analogy can be pushed too far.** Multijet and PF terms are not
  combine concepts; forcing them into `process`/`rate` columns or `rateParam`
  lines would make the card harder to read than today's code. Channel
  kinds (§4.3) are the place to stop.
* **L1L2Res** still has no reference (`toDo.txt` C); this design neither
  helps nor hurts.

---

## 11. Effect on the migration plan (`refact.md` §9)

Phases 0–4 are unchanged. Phase 5 (fit) splits into three, each with its own
parity check:

* **5a `makeCard`.** Port lines 354–1616. Check: the graphs and sources in
  `shapes.root` equal today's `_vdt1`, `_vsrc`, `_vpf*` (dump them once from
  the old code into the golden reference).
* **5b engine.** `Datacard`, `Workspace`, `Channel`s, `JesModel9`,
  `Chi2Function`, `FitMinimizer`, `-M FitDiagnostics`. Check: χ²,
  parameters, `emat`, `hjesfit*` against the golden outputs.
* **5c plots.** `plotFit` from `fit.root`.

After Phase 6, in this order: `--mask` / `--freezeParameters`,
`GoodnessOfFit`, `Impacts`, `combineCards`, `MultiDimFit`, toys. Each is new
functionality, so each is a separate commit with no parity requirement.

---

## 12. Questions for you

| # | question | my suggestion |
|---|---|---|
| 1 | Use this for the fit half, with `refact.md` kept for the input half? | Yes, if combined fits or routine cross-checks are in scope; otherwise stay with `refactClass.md`. |
| 2 | Datacard as combine-style text, or JSON like the rest of `config/`? | Text, generated by `makeCard` (§4.4). |
| 3 | FSR correction and source scaling applied in `makeCard` (fit-ready `shapes.root`) or inside the engine? | In `makeCard` (§3). |
| 4 | Several executables in `bin/` plus the `runFit` driver, or one `runFit` with sub-commands (`runFit card`, `runFit fit`)? | Several, plus the driver (§9). |
| 5 | Workspace saved as a file, or built in memory from the card each time? | Saved; it is small and it is the reproducibility unit (§5). |
| 6 | Executable names: combine's (`combine`, `text2workspace`, `combineCards`) or our own (`jecFit`, `jecWorkspace`, `jecCombineCards`)? | Our own, to avoid a clash with a real `combine` on `$PATH` in a CMSSW environment. |
| 7 | Which methods beyond `FitDiagnostics` do you want first? | `GoodnessOfFit` and `Impacts`. |
| 8 | New output directory `card/`, or put the card and workspace under `forfit/<corLevel>/<epoch>/`? | `card/`, so `forfit/` stays "ROOT files made by steps" and the contract is easy to find. |
