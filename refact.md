# JecSys refactor — design proposal (draft 1, 2026-11-18)

Goal: turn the macro chain behind `runMe.sh` into a compiled, config-driven
package (`runFit`) that is flexible, maintainable, understandable and, above
all, hard to break. This is a discussion draft; §10 lists the decisions we
need to make before implementing.

---

## 1. What we have today (facts that drive the design)

The chain, per epoch (`mk_reprocess_RunEpoch.C` → `mk_reprocess.C`):

| step | macro | reads | writes |
|---|---|---|---|
| 1 | `reprocess.C` (4549 lines) | 11 external ROOT files per epoch + JEC txt | `rootfiles/jecdata<epoch>.root` (RECREATE) |
| 2 | `softrad3.C` ×2 (\|η\|<1.3, <2.5) | jecdata | same file (UPDATE): `hdm_*`, `fsr/hkfsr3_*` |
| 3 | `globalFitSyst.C` | jecdata, 2018 γ gain file, hadW stand-in, `jecdataRun2Test.root` | same file: `sys/*`, `fsr/hadw_fsr`, `hdm_*` |
| 4 | `globalFitRenormPF.C` | jecdata | same file: `*_ren`, `*_cmb*` (not used by the fit in the DP_2021 config) |
| 5 | `globalFitL3Res.C` (3684 lines) | jecdata of epoch **and of its refIOV** | same file: `sys/emat`, `hjesfit*`, `fsr/hkfsr2_*`; `globalFitL3Res_emat_<epoch>.root`; 8 plots; one line appended to `textFiles/globalFitL3Res.txt` |

Problems, with evidence from the current code:

1. **Hidden mutable global state.** 103 file-scope settings in
   `globalFitL3Res.C`, 78 in `reprocess.C`; the χ² function reads globals.
   Behaviour depends on edits scattered over thousands of lines.
2. **Data logic hard-coded as `if (epoch==...)` chains.** `reprocess.C` has
   44 epoch comparisons, 218 uses of `isUL16/isAPV/...` flags, and 136
   `new TFile` call sites, while each of the four epochs we run opens only 11
  of them.
   Adding or changing an epoch means editing code in many places.
3. **One file mutated by four programs.** Each step opens
   `jecdata<epoch>.root` in UPDATE mode; `reprocess.C` RECREATEs it *before*
   checking its inputs, so a failed run destroys the previous result (this
   happened during the JecSys validation).
4. **The naming contract `<dir>/eta<..>/<type>_<set>_a<α>` is duplicated** as
   hand-written `Form(...)` strings in every macro (155 in reprocess, 147 in
   the fit). Settings are copied by hand (`multijetModeS` = copy of
   `multijetMode`).
5. **Hidden dependencies.** 2016GH reads 2016BCDEF's output (refIOV);
   every epoch reads the archived Run 2 output `jecdataRun2Test.root`; the
   2018 photon-gain profile, UL17 L1 placeholders and an EOY2017 Zee
   covariance are used for all years. None of this is visible without
   reading code.
6. **Magic numbers.** Physics inputs (e.g. `f1mjb` = 1.002 + 99.07·pT^-1.97,
   Ru = 0.685, γ scale 0.995/1.005, fit shape functions, hadW fit-prob
   parameters) live in code, with their origin documented only in comments.
7. **Fragile build.** ACLiC with `#include "tdrstyle_mod15.C"` in three
   libraries and `tools.C` both loaded and `#include`d. The global `TString`s
   in `tdrstyle_mod15.C` exist three times, which very likely causes the
   `double free or corruption` at exit (to be confirmed by the new build).
   Errors are `assert`s, live only because of the `+g` flag.
8. **Small latent bugs/dead code** (from the stage docs): `getY` never
   updates `dx`; `|` instead of `||`; `"RAED"`; unused `dodijet` argument;
   `th2d_jeteta_zpt.root` loaded but unused; `hruncl` written but never read;
   nuisance sources 17/19 and 18/20 identical (fit cannot separate them);
   the fit result is exported as an appended C++ snippet.

---

## 2. Proposed layout

Your directories, plus `external/` (vendored third-party code, kept
unchanged) and `test/`. Sub-folders inside `header/` and `cpp/` mirror each
other so a module is easy to find.

```
JecSys/
├── Makefile                 builds runFit (and compareRoot) from obj/; `make test`
├── main.cpp                 CLI only: parse args → load config → run steps
├── README.md
├── config/                  ALL data logic, JSON, tracked in git
│   ├── inputs.json          epoch registry: era, refIOV, input files, JEC tags  (runFit -h reads this)
│   ├── reprocess.json       per-channel native object names, pT windows, cleaning, corrections
│   ├── softrad.json         HDM/FSR settings (Ru, Rn, variations, samples)
│   ├── syst.json            external systematic parametrisations (with provenance)
│   └── fit.json             samples, methods, model, PF terms, nuisance sources, minimizer
├── header/                  .h
│   ├── core/                Config, Paths, Names, RootIO, Log, Errors, Step
│   ├── channels/            one reader per input format: ZllKIT, ZjetHelsinki, GamJet, Multijet, HadW, IncJet, PFJet
│   ├── steps/               Reprocess, JecBands, SoftRad, Syst, RenormPF, GlobalFit, Plot
│   ├── fit/                 JesModel, FitData, Chi2, FitResult
│   └── util/                GraphOps (old tools), Flavor, TdrStyle
├── cpp/                     .cpp, same sub-folders as header/
├── external/
│   ├── CondFormats/JetMETObjects/   standalone JEC classes, unchanged (keeps its include paths)
│   └── nlohmann/json.hpp            single header (also in /usr/include; vendored for portability)
├── test/                    unit tests + regression test against golden outputs
├── prefit/   (git-ignored)  read-only external inputs
│   ├── DP2021033/  DP2021033V2/  JERCProtoLab/ (copy or symlink)
│   ├── jec/                 JEC .txt (was CondFormats/JetMETObjects/data)
│   ├── flavor/              flavour L2Relative .txt (was rootfiles/flavor)
│   └── archive/             jecdataRun2Test.root, hadW stand-in, ...
├── forfit/   (git-ignored)  forfit/<epoch>/{reprocess,jecbands,softrad,syst,renormpf}.root
├── postfit/  (git-ignored)  postfit/<epoch>/{fit.root, result.json, config.resolved.json, run.log, plots/}
└── obj/      (git-ignored)
```

`header/` + `cpp/` is fine; the more common names are `include/` + `src/`
(your call, §10).

---

## 3. Data flow

```
prefit/ (external, read-only)
   │
   ├─ reprocess ─▶ forfit/<e>/reprocess.root   harmonised data/mc/ratio graphs (+ orig/)
   ├─ jecbands  ─▶ forfit/<e>/jecbands.root    herr*, hjes, hl1*, hsys*  (split out of reprocess §3.8)
   ├─ softrad   ─▶ forfit/<e>/softrad.root     hdm_*, fsr/hkfsr3_*
   ├─ syst      ─▶ forfit/<e>/syst.root        sys/*, fsr/hadw_fsr, hdm_gamjet, ghdm_*
   ├─ renormpf  ─▶ forfit/<e>/renormpf.root    *_ren, *_cmb*   (optional, not used by the fit today)
   │
   ├─ fit       ─▶ postfit/<e>/fit.root        sys/emat, hjesfit*, fsr/hkfsr2_*; result.json
   └─ plot      ─▶ postfit/<e>/plots/*.pdf     (re-plot without re-fitting)
```

Rules:

* **Each step writes only its own file** and never modifies another
  step's output. Steps become idempotent, restartable and independently
  testable.
* **Atomic writes:** write `X.root.tmp`, then rename to `X.root` on success.
  A crash never leaves a truncated or half-updated result.
* **Inside the files, object paths stay as today** (`ratio/eta00-13/mpfchs1_zjet_a100`,
  `fsr/hkfsr3_*`, `sys/*`), so the old docs and the regression comparison
  still apply.
* **Cross-epoch dependencies are declared**, not implicit: `refIOV` in
  `inputs.json`; `runFit` checks `forfit/<refIOV>/syst.root` exists (or runs
  it first with `--with-deps`); `-e all` runs epochs in dependency order.

---

## 4. Configuration

### 4.1 Principles

* **Code = algorithms and functional forms. Config = which data, which
  settings, which numbers.** Adding an epoch or swapping an input file must
  not need a recompile.
* **Defaults + overrides.** Each JSON has a `default` block and overrides
  keyed by era (`UL16APV`, `UL16`, `UL17`, `UL18`) or epoch. The
  resolution order is epoch → era → default, so a setting is written once.
* **Provenance in the data.** JSON has no comments; every physics number
  carries `_note` / `_source` fields (e.g. `"_source": "minitools/drawFlavor.C"`).
  `DP_2021` reverts become explicit flags with a note instead of commented code.
* **Validated at load time.** Unknown keys, unknown sample/type names,
  missing files → clear error before anything runs.
* **Provenance of the run.** The fully resolved config is written to
  `postfit/<epoch>/config.resolved.json`, together with the git hash.

### 4.2 `inputs.json` (sketch, values from the 2016BCDEF walkthrough)

```json
{
  "dirs": { "prefit": "prefit", "forfit": "forfit", "postfit": "postfit" },

  "common": {
    "_note": "Inputs shared by all epochs; mismatches with the epoch are deliberate DP_2021 placeholders",
    "ptEtaWeights": { "file": "DP2021033/P8_dijet_45M_TH2D_correct_weighting.root", "object": "pTjet_etajet" },
    "gamGain":      { "file": "DP2021033V2/GamHistosFill_data_2018ABCD_v19.root", "_note": "2018 data used for every year" },
    "hadwFsrMC":    { "file": "archive/hadWMC16GHV7_JEC.root", "_note": "stand-in rebuilt from jecdataRun2Test" },
    "run2Archive":  { "file": "archive/jecdataRun2Test.root", "object": "ratio/eta00-13/hdm_cmb_mj" },
    "flavorDir":    "flavor"
  },

  "epochs": {
    "2016BCDEF": {
      "era": "UL16APV",
      "refIOV": "2016BCDEF",
      "corLevel": "L1L2L3Res",
      "channels": {
        "multijet": { "file": "JERCProtoLab/Summer19UL16/L3Residual_multijet/multijet_UL2016BCDEF_jecV7_jerV2_closure.root" },
        "incjet":   { "file": "DP2021033/drawDeltaJEC_Run2_vsUL5X.root", "object": "jet_RunUL16APV_fwd" },
        "pfjet":    { "data": "DP2021033/output-DATA-2b-UL16V5V2_BCDEF.root",
                      "mc":   "DP2021033/output-MCNU-2b-UL16V3V1_BCDEF.root" },
        "hadw":     { "file": "DP2021033/hadW16APVV7_JEC.root" },
        "gamjet":   { "file": "DP2021033/GamHistosRatio_2016BCDEF_P8APV_v18.root" },
        "zeejet":   { "file": "JERCProtoLab/Summer19UL16/L3Residual_Z/APV/JEC_Combination_Zee/ZJetCombination_Zee_DYJets_amc_21Feb2020_Summer19UL16_V7_L1L2L3Res.root",
                      "subdir": "Run2016preVFPBCDEFearly" },
        "zmmjet":   { "file": "JERCProtoLab/Summer19UL16/L3Residual_Z/APV/JEC_Combination_Zmm/ZJetCombination_Zmm_DYJets_amc_21Feb2020_Summer19UL16_V7_L1L2L3Res.root",
                      "subdir": "Run2016preVFPBCDEFearly" },
        "zjet":     { "file": "DP2021033/jme_bplusZ_merged_v36_2016bf_emu_wTTJets.root" }
      },
      "jec": {
        "nominal": "Summer19UL16APV_RunBCDEF_V7_DATA_L2L3Residual",
        "lumiWeighted": [ { "tag": "Summer19UL16APV_RunBCD_V7_DATA_L2L3Residual", "lumi": 12.9 },
                          { "tag": "Summer19UL16APV_RunEF_V7_DATA_L2L3Residual",  "lumi": 7.2 } ],
        "uncertaintySources": "Summer19UL16APV_RunBCDEF_V7_DATA_UncertaintySources"
      },
      "fit": { "samples": "MJDJ_gam_z_hadw", "methods": "PtBalMPF",
               "incjetReference": { "file": "DP2021033V2/drawDeltaJEC_16ULJECV5_vsBCDEF.root",
                                    "object": "jet_Run16ULBCDEF_fwd" } }
    },
    "2016GH":    { "era": "UL16", "refIOV": "2016BCDEF", "...": "..." },
    "2017BCDEF": { "era": "UL17", "refIOV": "2017BCDEF", "...": "..." },
    "2018ABCD":  { "era": "UL18", "refIOV": "2018ABCD",  "...": "..." }
  }
}
```

### 4.3 The other config files (shape only)

* `reprocess.json` holds the per-channel native name templates (today's
  `rename[set][type]` map, e.g. `"{Dir}_MPF_CHS_a{alpha}_eta_{eta}_{corLevel}"`),
  per-channel pT windows, cleaning (`maxError: 0.2`, `minEvents: 20`),
  corrections (Z/γ mass, γ scale per era, multijet recoil-scale parameters
  with `_source`), the snipe list (epoch, sample, pT), and the processing
  axes (types × sets × η × α, with today's `continue` rules written as data).
* `softrad.json` holds Ru, Rn, their variations (+0.08/+0.05, −0.01/−0.02),
  the samples per η range, and the reference α per sample.
* `syst.json` holds each external systematic (multijet JER, Zee covariance,
  γ gain, hadW fit-prob, hadW FSR) with its parameters and `_source`.
* `fit.json` holds the sample/method aliases, α per sample, FSR switches,
  PF terms (`use`, `delta`, `ptRange`), the model (active parameters,
  priors), `ptMin`, the **nuisance-source list with explicit targets**,
  and minimizer settings:

```json
"sources": [
  { "name": "fsru1", "from": "softrad:mpfu1", "appliesTo": "each" },
  { "name": "scale_020_gamjet", "type": "flat", "value": 0.0020,
    "appliesTo": ["ptchs_gamjet", "mpfchs1_gamjet"] },
  { "name": "eesfromzee_gamjet_eig{0,1,2}", "from": "syst:zee_gamjet_eig",
    "appliesTo": ["ptchs_gamjet", "mpfchs1_gamjet"] }
]
```

  This replaces the bitmask encoded in the histogram name (`bm34_...`).

---

## 5. `runFit` command line

```
$ ./runFit -h
runFit - JEC L3Res global-fit chain

Usage: runFit -e <epoch|all> [-s <step>[,<step>...]] [options]

Steps (in order): reprocess jecbands softrad syst renormpf fit plot   (default: all)
  -e, --epoch <name>     epoch from config/inputs.json, or 'all' (dependency order)
  -s, --step  <list>     steps to run
      --from  <step>     run <step> and every later step
      --with-deps        also run missing refIOV steps first
      --check            verify every input file and object exists; run nothing
      --config <dir>     config directory (default: config/)
  -v, --verbose          debug printout (e.g. every removed point)
  -h, --help             this message

Epochs in config/inputs.json:
  2016BCDEF  UL16APV  refIOV self        ./runFit -e 2016BCDEF
  2016GH     UL16     refIOV 2016BCDEF   ./runFit -e 2016GH      (needs forfit/2016BCDEF/syst.root)
  2017BCDEF  UL17     refIOV self        ./runFit -e 2017BCDEF
  2018ABCD   UL18     refIOV self        ./runFit -e 2018ABCD
Run everything: ./runFit -e all
```

`runMe.sh` becomes `./runFit -e all`. `--check` is the cheap preflight
that would have caught the missing-file problems before anything was
overwritten.

---

## 6. Code architecture

### 6.1 Core (used by every step)

* `Config`: loads and validates the JSON and resolves epoch → era → default.
  It returns typed structs (`EpochConfig`, `ReprocessConfig`, `FitConfig`, ...),
  so no step ever sees raw JSON.
* `Names`: the **single** implementation of the object-naming contract,
  built from typed enums:
  `names::graph(Dir::Ratio, Eta::B13, Type::MpfChs1, Sample::ZJet, Alpha::A100)`
  → `"ratio/eta00-13/mpfchs1_zjet_a100"`. Enums have `toString`/`fromString`,
  so a typo in config fails at load time instead of producing "not found" at
  line 2700.
* `RootIO`: `getRequired<T>(file, path)` throws with file and object name;
  `getOptional<T>`; returned objects are owned (`unique_ptr`, detached from
  directories); files are closed deterministically. This removes the
  ownership tangle behind the exit crash.
* `Errors` / `Log`: exceptions instead of `assert`, caught in `main` →
  clear message and non-zero exit code; log levels instead of 40k
  `REMOVE POINT` lines (still available with `-v`); a per-epoch `run.log`.
* `Step`: small interface (`name()`, `inputs()`, `outputs()`, `run()`), so
  `main` can list steps, check inputs and handle `--from`/`--with-deps`
  generically.

### 6.2 Reprocess, split by responsibility

* `channels/*`: one reader per input format. It knows that format's file
  layout and naming and returns harmonised `TGraphErrors`/count histograms.
  Today's per-channel `Form(...)` patterns and special cases (zlljet = merge
  of zee+zmm, pfjet ratio = data−MC, ...) move here.
* A common pipeline applied to every graph: *convert → keep orig → clean →
  patch → correct → write*. Each stage is a small function driven by
  `reprocess.json`, so it can be unit-tested.
* `JecBands`: today's §3.8 of reprocess (JEC central values, uncertainty
  bands, L1 systematics) as its own step. It needs only JEC text files and
  the pT–η weights, not the channel inputs.

### 6.3 SoftRad, Syst, RenormPF

These are ported as classes with their numbers moved to config. The HDM
solvers (`fm`, `fp`, `fma`, `fpa`) become pure functions with unit tests.

### 6.4 Global fit, without globals

* `FitData`: the response graphs (sample × method), the recoil pT for
  multijet, the PF fractions, and a list of `NuisanceSource{name,
  shape (TH1D), appliesTo (set of graph ids)}`.
* `JesModel`: the 9 parameters and their shape functions. The functional
  forms are in code; the coefficients and on/off switches come from `fit.json`.
* `Chi2`: a functor `double operator()(const double* p) const` that holds
  references to `FitData` and `JesModel`; `breakdown(p)` returns the
  per-dataset χ². Minimized through `ROOT::Math::Minimizer`. We keep
  TMinuit first for numerical parity, and moving to Minuit2 is a separate,
  validated change.
* `FitResult`: parameters, covariance, χ² breakdown, eigenvectors, written
  to `fit.root` **and** `result.json` (replaces the appended C++ line in
  `textFiles/globalFitL3Res.txt`).
* `Plot`: a separate step reading `fit.root`/forfit files → `plots/*.pdf`;
  `TdrStyle` becomes a normal compiled module with no globals in headers.

---

## 7. Build

```make
CXXFLAGS := $(shell root-config --cflags) -O2 -g -Wall -Wextra -Wshadow \
            -Iheader -Iexternal -MMD -MP
LDLIBS   := $(shell root-config --libs) -lMinuit -lMinuit2
SRC      := main.cpp $(shell find cpp -name '*.cpp') \
            $(filter-out %/Utilities.cc,$(wildcard external/CondFormats/JetMETObjects/src/*.cc))
OBJ      := $(patsubst %,obj/%.o,$(basename $(SRC)))

runFit: $(OBJ)
	$(CXX) $^ -o $@ $(LDLIBS)
obj/%.o: %.cpp ; @mkdir -p $(@D) && $(CXX) $(CXXFLAGS) -c $< -o $@
obj/%.o: %.cc  ; @mkdir -p $(@D) && $(CXX) $(CXXFLAGS) -c $< -o $@
-include $(OBJ:.o=.d)
```

* C++17 (what ROOT 6.40 here is built with), and the build has to pass
  with zero warnings.
* `Utilities.cc` is `#include`d by three JEC `.cc` files, so it is not
  compiled on its own; if linking them into one binary gives duplicate
  symbols, we wrap it in an anonymous namespace (one-line change, the only
  edit to vendored code).
* No ACLiC and no dictionaries, so builds are fast and incremental, and
  `make` fails at compile time instead of at run time.

---

## 8. How this reduces bugs (mapping to §1)

| problem | remedy |
|---|---|
| global switches | typed config structs passed explicitly; `Chi2` is a functor, no globals |
| epoch `if` chains | `inputs.json` + era overrides; code has no epoch names |
| UPDATE-mode shared file, truncation | one output file per step, atomic rename, `--check` preflight |
| duplicated naming strings | `Names` + enums, one implementation |
| magic numbers | config with `_source`; resolved config saved per run |
| hidden dependencies | `refIOV` and `common` inputs declared; dependency check in `runFit` |
| ACLiC / ODR / exit crash | one linked binary, no `.C` includes, owned ROOT objects |
| `assert` error handling | exceptions with context, non-zero exit |
| small latent bugs | fixed in the port only where parity allows; the rest are listed and fixed as separate, flagged physics changes |
| identical nuisance sources | explicit source list makes duplicates visible; config validation can warn |

---

## 9. Validation and migration plan

**Acceptance criterion for every phase: identical physics output to
today's chain.** Without this, a refactor of this size will silently change
results.

* **Phase 0 (golden reference).** Freeze today's outputs (the four
  `jecdata<epoch>.root`, `globalFitL3Res_emat_<epoch>.root`, `pdf/*/*.root`,
  χ²/parameter printouts) as the reference. Write `compareRoot`: it walks two sets of
  files, matches objects by path, and compares every TH1/TGraph/TMatrix/TVector/TF1
  (relative tolerance ~1e-9 for histograms, ~1e-6 for fit results). Hook it
  into `make test`.
* **Phase 1 (mechanical move).** New layout + Makefile + `runFit` calling the
  existing functions with minimal edits (no ACLiC, no `.C` includes,
  paths via `Paths`). Check parity and that the exit crash is gone.
* **Phase 2 (inputs config).** `inputs.json`, `runFit -h/--check`, epoch file
  selection out of code; per-step output files and atomic writes.
* **Phase 3 (reprocess).** Channel readers + common pipeline + `JecBands`;
  `reprocess.json`; `Names`.
* **Phase 4 (softrad, syst, renormpf).** Classes and config.
* **Phase 5 (fit).** `FitData`/`JesModel`/`Chi2`/`FitResult`, explicit
  sources, separate `plot` step.
* **Phase 6 (cleanup).** Remove dead branches, unit tests, docs. Only after
  this: deliberate physics changes (the `DP_2021 (bug!)` items), each as its
  own flagged commit with a before/after comparison.

Each phase is one or more commits that pass `make test`.

---

## 10. Open questions for round 1

My recommendation is given in brackets after each question.

1. **Parity:** must every phase reproduce today's numbers exactly, with
   physics fixes postponed to after the refactor? [Yes.]
2. **Epoch scope:** only the 4 reference IOVs, or also the sub-IOVs (2016BCD,
   2018A, ...), 2017H (low PU) and Run2Test / `globalFitRun2.C`? This decides
   how much of the 136-file `if` maze to carry over. [The 4 IOVs now; the
   config design allows adding the others as data later.]
3. **Per-step output files** (§3) instead of one shared `jecdata<epoch>.root`?
   [Per-step files.]
4. **Legacy options:** drop `CorLevel = L1L2Res`, `softrad.C` (α
   extrapolation), alternative fit models (`njesFit != 9`), `setFullShapeFuncs`,
   unused samples/outputs (`hruncl`, `th2d_jeteta_zpt`)? [Drop. They stay
   available in jecsys history.]
5. **`renormpf`:** keep as an optional step (not used by the fit today), or
   drop until `globalFitRun2` is ported? [Keep, optional, not in `all`.]
6. **Names:** `header/` + `cpp/` (your proposal) or `include/` + `src/`? Flat
   or with sub-folders? [Your names, with sub-folders.]
7. **Config granularity:** one `inputs.json` for everything, or split by
   concern as in §2? [Split; `inputs.json` stays the epoch registry that
   `-h` reads.]
8. **External `../JERCProtoLab` inputs (10 files):** copy into
   `prefit/JERCProtoLab/` or point to them by path in config? [Copy, so
   `prefit/` is self-contained.]
9. **Minimizer:** TMinuit for parity, Minuit2 later? [Yes.]
10. **Plotting:** stay in C++ (ROOT) or move to Python (uproot + matplotlib)
    later? [C++ now, since the plots are part of the parity check.]
11. **Build system:** Makefile (as you proposed) or CMake? [Makefile; it is
    enough for one binary.]
12. **Inputs on EOS:** should `dirs.prefit` be able to point to EOS / another
    machine? [Yes, it's just a path in config.]

### Alternatives considered

* **Python driver + C++ core** (CLI, config and plots in Python; physics in
  compiled C++). It's attractive for plotting, but it adds a second language
  and environment; we can revisit after Phase 6.
* **Pipeline steps as Make targets** (`make forfit/2016GH/softrad.root`).
  This gives automatic incremental re-running, but mixes building the code
  with running the physics; `runFit`'s own dependency check is clearer.
