# JecSys refactor — design proposal (draft 3, converged, 2026-11-18)

Goal: turn the macro chain behind `runMe.sh` into a compiled, config-driven
package (`runFit`) that is flexible, maintainable, understandable and, above
all, hard to break. This is a discussion draft.

* **§10 records the round-1 decisions.**
* **`refactClass.md`** holds the class design and the camelCase naming
  convention, and **replaces §6** below.
* **`toDo.txt`** lists the physics fixes postponed until after the refactor.

Where the round-1 answers changed the text below, it is marked *(round 2)*.

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

*(round 2)* Your directories, flat, plus `test/`. Each class has a matching
`header/X.h`, `cpp/X.cpp` and, if it has settings, `config/X.json`
(`refactClass.md` §2 and §6). The vendored CMSSW JEC classes are flat too
(`refactClass.md` §4.6), so there is no `external/` directory.

```
JecSys/
├── Makefile                 builds runFit (and compareRoot) from obj/; make test / lint / format
├── main.cpp                 CLI only: CommandLine → Pipeline
├── README.md  refact.md  refactClass.md  toDo.txt
├── .clang-format  .clang-tidy
├── config/                  ALL data logic, JSON, tracked in git, one file per class
│   ├── Inputs.json          epoch registry: era, refIov, corLevel, input files + reader, JEC tags, roots  (runFit -h)
│   ├── Pipeline.json        step order; which steps `all` runs
│   └── Reprocess.json  ZllJetReader.json ... SoftRad.json ... FitData.json  JesModel.json ...   (refactClass.md §6)
├── header/                  all .h, flat (38 own + 6 vendored JEC files)
├── cpp/                     all .cpp, flat
├── test/                    unit tests + regression test against golden outputs
├── prefit/   (git-ignored)  read-only external inputs that we keep locally
│   ├── DP2021033/  DP2021033V2/
│   ├── jec/                 JEC .txt (was CondFormats/JetMETObjects/data)
│   ├── flavor/              flavour L2Relative .txt (was rootfiles/flavor)
│   └── archive/             jecdataRun2Test.root, hadW stand-in, ...
│                            ../JERCProtoLab is NOT copied: referenced as ${jercProtoLab} in Inputs.json
├── forfit/   (git-ignored)  forfit/<corLevel>/<epoch>/{reprocess,jecBands,softRad,syst,renormPf}.root
│                            + forfit_<corLevel>_<epoch>.root  (all of the above combined; replaces jecdata<epoch>.root)
├── postfit/  (git-ignored)  postfit/<corLevel>/<epoch>/{fit.root, result.json, config.resolved.json, run.log, plots/}
└── obj/      (git-ignored)
```

---

## 3. Data flow

*(round 2)* `<e>` below stands for `<corLevel>/<epoch>`, e.g.
`L1L2L3Res/2016GH`. Step and file names are camelCase.

```
prefit/ + ${jercProtoLab} (external, read-only)
   │
   ├─ reprocess ─▶ forfit/<e>/reprocess.root   harmonised data/mc/ratio graphs (+ orig/)
   ├─ jecBands  ─▶ forfit/<e>/jecBands.root    herr*, hjes, hl1*, hsys*  (split out of reprocess §3.8)
   ├─ softRad   ─▶ forfit/<e>/softRad.root     hdm_*, fsr/hkfsr3_*
   ├─ syst      ─▶ forfit/<e>/syst.root        sys/*, fsr/hadw_fsr, hdm_gamjet, ghdm_*
   ├─ renormPf  ─▶ forfit/<e>/renormPf.root    *_ren, *_cmb*   (optional, not used by the fit today)
   │      └─ (Pipeline) ─▶ forfit/<e>/forfit_<corLevel>_<epoch>.root   union of the step files above
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
* **Cross-epoch dependencies are declared**, not implicit: `refIov` in
  `Inputs.json`; `runFit` checks `forfit/<corLevel>/<refIov>/syst.root` exists
  (or runs it first with `--with-deps`); `-e all` runs epochs in dependency
  order.

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
  `postfit/<corLevel>/<epoch>/config.resolved.json` (and in `_meta/` of each output file), together with the git hash.

### 4.2 `Inputs.json` (sketch, values from the 2016BCDEF walkthrough)

*(round 2)* camelCase keys; named roots with `${...}` expansion; the reader
for each channel is named in the file; files can be given per `corLevel`.

```json
{
  "roots": { "prefit": "prefit", "jercProtoLab": "../JERCProtoLab",
             "forfit": "forfit", "postfit": "postfit" },

  "common": {
    "_note": "Inputs shared by all epochs; mismatches with the epoch are deliberate DP_2021 placeholders (toDo.txt B)",
    "ptEtaWeights": { "file": "${prefit}/DP2021033/P8_dijet_45M_TH2D_correct_weighting.root", "object": "pTjet_etajet" },
    "gamGain":      { "file": "${prefit}/DP2021033V2/GamHistosFill_data_2018ABCD_v19.root", "_note": "2018 data used for every year" },
    "hadwFsrMc":    { "file": "${prefit}/archive/hadWMC16GHV7_JEC.root", "_note": "stand-in rebuilt from jecdataRun2Test" },
    "run2Archive":  { "file": "${prefit}/archive/jecdataRun2Test.root", "object": "ratio/eta00-13/hdm_cmb_mj" },
    "flavorDir":    "${prefit}/flavor",
    "jecDir":       "${prefit}/jec"
  },

  "epochs": {
    "2016BCDEF": {
      "era": "UL16APV",
      "refIov": "2016BCDEF",
      "corLevel": "L1L2L3Res",
      "channels": {
        "multijet": { "reader": "MultijetReader",
                      "file": { "L1L2L3Res": "${jercProtoLab}/Summer19UL16/L3Residual_multijet/multijet_UL2016BCDEF_jecV7_jerV2_closure.root",
                                "L1L2Res":   "${jercProtoLab}/Summer19UL16/L3Residual_multijet/multijet_UL2016BCDEF_jecV5_jerV2.root" } },
        "incjet":   { "reader": "IncJetReader", "file": "${prefit}/DP2021033/drawDeltaJEC_Run2_vsUL5X.root", "object": "jet_RunUL16APV_fwd" },
        "pfjet":    { "reader": "PfJetReader",
                      "data": "${prefit}/DP2021033/output-DATA-2b-UL16V5V2_BCDEF.root",
                      "mc":   "${prefit}/DP2021033/output-MCNU-2b-UL16V3V1_BCDEF.root" },
        "hadw":     { "reader": "HadWReader",   "file": "${prefit}/DP2021033/hadW16APVV7_JEC.root" },
        "gamjet":   { "reader": "GamJetReader", "file": "${prefit}/DP2021033/GamHistosRatio_2016BCDEF_P8APV_v18.root" },
        "zeejet":   { "reader": "ZllJetReader",
                      "file": "${jercProtoLab}/Summer19UL16/L3Residual_Z/APV/JEC_Combination_Zee/ZJetCombination_Zee_DYJets_amc_21Feb2020_Summer19UL16_V7_L1L2L3Res.root",
                      "subdir": "Run2016preVFPBCDEFearly" },
        "zmmjet":   { "reader": "ZllJetReader",
                      "file": "${jercProtoLab}/Summer19UL16/L3Residual_Z/APV/JEC_Combination_Zmm/ZJetCombination_Zmm_DYJets_amc_21Feb2020_Summer19UL16_V7_L1L2L3Res.root",
                      "subdir": "Run2016preVFPBCDEFearly" },
        "zjet":     { "reader": "ZJetReader",   "file": "${prefit}/DP2021033/jme_bplusZ_merged_v36_2016bf_emu_wTTJets.root" }
      },
      "jec": {
        "nominal": "Summer19UL16APV_RunBCDEF_V7_DATA_L2L3Residual",
        "lumiWeighted": [ { "tag": "Summer19UL16APV_RunBCD_V7_DATA_L2L3Residual", "lumi": 12.9 },
                          { "tag": "Summer19UL16APV_RunEF_V7_DATA_L2L3Residual",  "lumi": 7.2 } ],
        "uncertaintySources": "Summer19UL16APV_RunBCDEF_V7_DATA_UncertaintySources"
      },
      "fit": { "samples": "MJDJ_gam_z_hadw", "methods": "PtBalMPF",
               "incjetReference": { "file": "${prefit}/DP2021033V2/drawDeltaJEC_16ULJECV5_vsBCDEF.root",
                                    "object": "jet_Run16ULBCDEF_fwd" } }
    },
    "2016GH":    { "era": "UL16", "refIov": "2016BCDEF", "...": "..." },
    "2017BCDEF": { "era": "UL17", "refIov": "2017BCDEF", "...": "..." },
    "2018ABCD":  { "era": "UL18", "refIov": "2018ABCD",  "...": "..." }
  }
}
```

### 4.3 The other config files

*(round 2)* One file per configurable class, with the same name as the class;
the list and the common shape (`default` / `eras` / `epochs`) are in
`refactClass.md` §6. The content is what draft 1 described: native name
templates and pT windows (now in the `*Reader.json` files), cleaning
(`GraphCleaner.json`), corrections (`GraphCorrector.json`), HDM settings
(`SoftRad.json`), external systematics (`GlobalFitSyst.json`), and the fit
(`FitData.json`, `JesModel.json`, `FitMinimizer.json`).

The **nuisance-source list with explicit targets** lives in `FitData.json`:

```json
"sources": [
  { "name": "fsru1", "from": "softRad:mpfu1", "appliesTo": "each" },
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

Steps (in order): reprocess jecBands softRad syst [renormPf] fit plot
                  (default: all; renormPf is optional and runs only with -s)
  -e, --epoch <name>      epoch from config/Inputs.json, or 'all' (dependency order)
  -s, --step  <list>      steps to run
      --from  <step>      run <step> and every later step
      --corLevel <level>  L1L2L3Res (default from Inputs.json) or L1L2Res
      --with-deps         also run missing refIov steps first
      --check             verify every input file and object exists; run nothing
      --config <dir>      config directory (default: config/)
  -v, --verbose           debug printout (e.g. every removed point)
  -h, --help              this message

Epochs in config/Inputs.json:
  2016BCDEF  UL16APV  refIov self        ./runFit -e 2016BCDEF
  2016GH     UL16     refIov 2016BCDEF   ./runFit -e 2016GH      (needs forfit/L1L2L3Res/2016BCDEF/syst.root)
  2017BCDEF  UL17     refIov self        ./runFit -e 2017BCDEF
  2018ABCD   UL18     refIov self        ./runFit -e 2018ABCD
Run everything: ./runFit -e all
```

`runMe.sh` becomes `./runFit -e all`. `--check` is the cheap preflight
that would have caught the missing-file problems before anything was
overwritten.

---

## 6. Code architecture

*(round 2)* Replaced by **`refactClass.md`**: class map, interfaces, design
rules, camelCase naming convention and rename table. The draft-1 ideas
(`Config`, `Names`, `RootIO`, `Errors`/`Log`, `Step`, channel readers,
`JecBands`, `FitData`/`JesModel`/`Chi2`/`FitResult`, `Plot`) are all there,
under their final class names (`ConfigFile`, `GraphKey`,
`InputFile`/`OutputFile`, `JecSysError`/`Logger`, `Step`, `*Reader`,
`JecBands`, `FitData`/`JesModel`/`Chi2Function`/`FitMinimizer`/`FitResult`,
`FitPlotter`).

---

## 7. Build

*(round 2)* Flat layout; the vendored JEC `.cc` files become `cpp/*.cpp`, so
one pattern rule covers everything.

```make
JSON_INC ?=                       # dir holding nlohmann/json.hpp, only if not in a default path (e.g. LCG view)
CXXFLAGS := $(shell root-config --cflags) -O2 -g -Wall -Wextra -Wshadow \
            -Iheader $(if $(JSON_INC),-isystem $(JSON_INC)) -MMD -MP
LDLIBS   := $(shell root-config --libs) -lMinuit -lMinuit2
JEC_VENDORED := FactorizedJetCorrector JetCorrectorParameters SimpleJetCorrector \
                JetCorrectionUncertainty SimpleJetCorrectionUncertainty
VENDORED := $(JEC_VENDORED:%=cpp/%.cpp) $(JEC_VENDORED:%=header/%.h) header/JetMetUtilities.h
SRC      := main.cpp $(wildcard cpp/*.cpp)
OBJ      := $(patsubst %.cpp,obj/%.o,$(notdir $(SRC)))
vpath %.cpp . cpp

runFit: $(OBJ)
	$(CXX) $^ -o $@ $(LDLIBS)
obj/%.o: %.cpp ; @mkdir -p obj && $(CXX) $(CXXFLAGS) -c $< -o $@
-include $(OBJ:.o=.d)

lint:   ; clang-tidy $(filter-out $(VENDORED),$(SRC)) -- $(CXXFLAGS)
format: ; clang-format -i main.cpp $(filter-out $(VENDORED),$(wildcard cpp/*.cpp header/*.h))
```

* C++17 (what ROOT 6.40 here is built with), and the build has to pass
  with zero warnings (vendored files are compiled with their warnings
  silenced).
* `Utilities.cc` already has an include guard and an anonymous namespace, so
  it becomes `header/JetMetUtilities.h` and causes no duplicate symbols.
* No ACLiC and no dictionaries, so builds are fast and incremental, and
  `make` fails at compile time instead of at run time.

---

## 8. How this reduces bugs (mapping to §1)

| problem | remedy |
|---|---|
| global switches | immutable config structs passed to constructors; `Chi2Function` is a const functor, no globals |
| epoch `if` chains | `Inputs.json` + era overrides; code has no epoch names |
| UPDATE-mode shared file, truncation | one output file per step, `OutputFile` atomic commit, `--check` preflight |
| duplicated naming strings | `GraphKey` + enums, one implementation |
| magic numbers | config with `_source`; resolved config saved per run and in `_meta/` |
| hidden dependencies | `refIov` and `common` inputs declared; dependency check in `Pipeline` |
| ACLiC / ODR / exit crash | one linked binary, no `.C` includes, owned ROOT objects |
| `assert` error handling | `JecSysError` with context, non-zero exit |
| inconsistent naming | camelCase convention enforced by clang-tidy (`make lint`) |
| small latent bugs | fixed in the port only where parity allows (`toDo.txt` D); the rest are listed in `toDo.txt` A–C and fixed later as separate, flagged physics changes |
| identical nuisance sources | explicit source list makes duplicates visible; config validation can warn |

---

## 9. Validation and migration plan

**Acceptance criterion for every phase: identical physics output to
today's chain** (decided in round 1). Physics fixes wait until after Phase 6
and are listed in `toDo.txt`.

* **Phase 0 (golden reference).** Freeze today's outputs (the four
  `jecdata<epoch>.root`, `globalFitL3Res_emat_<epoch>.root`, `pdf/*/*.root`,
  χ²/parameter printouts) as the reference. Write `compareRoot`: it walks two
  sets of files, matches objects by path, and compares every
  TH1/TGraph/TMatrix/TVector/TF1 (relative tolerance ~1e-9 for histograms,
  ~1e-6 for fit results). It ignores `_meta/`, and it takes a list of objects
  removed by design (e.g. `hruncl`). Hook it into `make test`. There is no
  L1L2Res reference, because its inputs are missing (`toDo.txt` C).
  Once Phase 2 exists, the comparison is one pair of files per epoch:
  `forfit_<corLevel>_<epoch>.root` plus `postfit/.../fit.root` against
  today's `jecdata<epoch>.root`.
* **Phase 1 (mechanical move).** Flat layout with `git mv` (history kept),
  Makefile, `runFit` calling the existing functions with minimal edits (no
  ACLiC, no `.C` includes, paths via config). **No renames yet.** Check parity
  and that the exit crash is gone.
* **Phase 2 (core + inputs).** `Inputs`, `ConfigFile`, `Pipeline`,
  `CommandLine` (`-h`, `--check`, `--corLevel`), `InputFile`/`OutputFile`;
  epoch file selection out of code; per-step output files and atomic writes.
* **Phase 3 (reprocess).** `ChannelReader`s, `GraphCleaner`, `GraphCorrector`,
  `JecLoader`, `JecBands`, `GraphKey`; their JSON files.
* **Phase 4 (softRad, syst, renormPf).** `SoftRad` (+ `hdm::`, `Flavor`),
  `GlobalFitSyst`, `GlobalFitRenormPf`.
* **Phase 5 (fit).** `FitData`, `JesModel`, `Chi2Function`, `FitMinimizer`
  (TMinuit), `FitResult`, `FitPlotter`, explicit sources.
* **Phase 6 (cleanup).** Remove dead branches, unit tests, docs, camelCase
  sweep, `make lint` clean. Then `toDo.txt` items one by one, each a flagged
  commit with a before/after comparison; then Minuit2.

camelCase is applied to each module when it is ported to its class (Phases
2–5), not in Phase 1, so the Phase 1 diff stays a pure move. Each phase is
one or more commits that pass `make test`.

---

## 10. Decisions from round 1 (2026-11-18)

| # | question | decision |
|---|---|---|
| 1 | Parity at every phase; physics fixes after | **Yes.** Fixes are listed in `toDo.txt`. |
| 2 | Epoch scope | **The 4 IOVs now**; keep the flexibility to add the others as config. |
| 3 | Per-step output files | **Yes.** |
| 4 | Legacy options | **Keep `CorLevel = L1L2Res`** (as `--corLevel`, see `refactClass.md` §6; inputs missing today, `toDo.txt` C). **Drop** `softrad.C`, `njesFit != 9`, `setFullShapeFuncs`, `hruncl`, `th2d_jeteta_zpt`. |
| 5 | `renormPf` | **Keep, optional** (not in `all`). |
| 6 | Directory names | **`header/` + `cpp/`, flat**, no sub-folders. |
| 7 | Config granularity | **One JSON per class, same name as the code** (`SoftRad.h/.cpp/.json`). |
| 8 | `../JERCProtoLab` | **Point to it by path in config** (`${jercProtoLab}`), no copy. |
| 9 | Minimizer | **TMinuit for parity, Minuit2 later.** |
| 10 | Plotting | **C++.** |
| 11 | Build system | **Makefile.** |
| 12 | Inputs on EOS | **Just a path in config.** |

New in round 2 (your additions):

* **camelCase** for functions, methods and variables (`refactClass.md` §3).
* **C++ class framework** (`refactClass.md`).

Round-2 decisions (member style `config_`, PascalCase file names, jecsys-like
step class names, one JSON per reader, `JecBands` as its own step,
`forfit/<corLevel>/<epoch>/` with a combined `forfit_<corLevel>_<epoch>.root`,
2-space indent and 100 columns) are in `refactClass.md` §8.

### Alternatives considered

* **Python driver + C++ core** (CLI, config and plots in Python; physics in
  compiled C++). It's attractive for plotting, but it adds a second language
  and environment; we can revisit after Phase 6.
* **Pipeline steps as Make targets** (`make forfit/2016GH/softrad.root`).
  This gives automatic incremental re-running, but mixes building the code
  with running the physics; `runFit`'s own dependency check is clearer.
