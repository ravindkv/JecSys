# JecSys as a C++ class framework (round 2, 2026-11-18)

Companion to `refact.md`. That file covers layout, data flow, the CLI and the
migration phases. This one covers the class design and the naming convention.
The decisions from round 1 are listed in `refact.md` §10.

---

## 1. My take

**Yes, a class-based design is the right fit, if we follow a few rules.**

Classes pay off in four places in this code:

| need | today | as a class |
|---|---|---|
| state with a lifetime | `new TFile(...)` that is never closed, outputs opened in UPDATE mode | `InputFile` / `OutputFile` close their file when they go out of scope (RAII); `OutputFile` writes to a temporary file and renames it only on `commit()` |
| one interface, several implementations | 7 input formats handled in one 4549-line function; 5 steps chained by a macro | `ChannelReader` with 7 subclasses; `Step` with 7 subclasses |
| settings checked once, then fixed | 180 file-scope globals | one `XxxConfig` struct per class, checked when it is loaded and `const` afterwards |
| values with rules | `Form("%s/eta%02.0f-%02.0f/%s_%s_a%1.0f", ...)` repeated about 300 times | a `GraphKey` value type with `path()` and `parse()` |

Classes do **not** pay off for pure math. Graph arithmetic, the HDM solvers
and the JES shape formulas stay as free functions in a namespace. A class
with no state is a namespace with extra syntax, and physicists should be able
to read a formula without going through object plumbing.

**The trap to avoid** is moving the 103 fit globals into the members of one
`GlobalFit` class. That compiles and looks object-oriented, but every method
can still change every setting, so the hidden coupling stays. The design below
prevents this by keeping three things apart:

* **settings** go into an immutable config struct;
* **data** goes into `FitData`, filled once;
* **behaviour** goes into small classes that receive both through their
  constructor and cannot change them.

**ROOT-specific points:**

* **No dictionaries.** Our classes are never written to ROOT files; we write
  only `TH1`, `TGraphErrors`, `TMatrixD`, `TVectorD`, `TF1` and `TNamed`. So we
  need no `ClassDef`, no `rootcling` and no `LinkDef`, and the Makefile stays
  a plain C++ build.
* **Ownership.** `TH1::AddDirectory(false)` is set once in `main`. An object
  read from a file is cloned into a `std::unique_ptr`, and a file is closed
  when its owner goes away. This is the part of today's code that causes the
  crash at exit.
* **TMinuit wants a C function pointer.** `TFitter::SetFCN` cannot take an
  object, so `FitMinimizer` keeps **one documented static pointer** to the
  active `Chi2Function`. It is the only static in the code base, and it
  disappears when we move to `ROOT::Math::Minimizer` with a functor
  (Minuit2, after parity).
* **Evaluating the χ² must not modify anything.** Today `jesFitter` calls
  `_jesFit->SetParameter` and `TF1::EvalPar`, which is non-const, on every
  call. `JesModel::evaluate(pt, par)` will call the same C++ formula
  directly, giving the same numbers. The `TF1` wrappers are built only for
  plotting.
* **Global style state.** `gStyle` and `gPad` are global by ROOT's design;
  they are touched only inside `TdrStyle` and `FitPlotter`.

---

## 2. Design rules

1. **One main class per file pair, named after the class:** `header/SoftRad.h`
   and `cpp/SoftRad.cpp`. Small types that belong to it, such as its config
   struct or an enum, live in the same files.
2. **One config file per class, with the same name:** `config/SoftRad.json` is
   read only by `SoftRad`. A class without settings has no JSON file. So
   `grep -l SoftRad header cpp config` finds everything.
3. **`Inputs.json` says *what* to read** (files per epoch). **`<Class>.json`
   says *how*** (settings and numbers).
4. **Dependencies are passed in the constructor.** No globals and no
   singletons, with the single exception of the TMinuit callback pointer
   (§1).
5. **Inheritance is used only for the two interfaces, `Step` and
   `ChannelReader`,** and only one level deep. Everything else is
   composition.
6. **The code never compares epoch or era names.** Differences between years
   are config overrides. A difference in input *format* is handled by choosing
   a reader in `Inputs.json` (`"reader": "ZllJetReader"`). Branches on
   `CorLevel` (2 values) are allowed, because those are real algorithm
   differences.
7. **Errors throw `JecSysError`** with context (step, epoch, file, object).
   `main` catches it, prints the message and exits with code 1. `assert` is
   used only for programmer invariants, never to validate input.
8. **ROOT objects have a clear owner:** a `unique_ptr`, detached from any
   directory.
9. **Physics formulas are short, documented free functions** that cite their
   source (for example `// fit to UL18 multijet, minitools/drawRecoil.C`).
10. **Header hygiene:** `#pragma once`, no `using namespace` in headers, and
    forward declarations where they are enough.
11. **Every class can be tested without input files.** It can be built from a
    config struct and in-memory graphs.
12. **Provenance:** each step writes its resolved config and the git hash into
    `_meta/` in its output file. Downstream steps read upstream settings from
    there, which removes the hand copy `multijetModeS` = `multijetMode`.
    `compareRoot` ignores `_meta/`.

---

## 3. Naming convention (camelCase)

| kind | style | examples |
|---|---|---|
| class, struct, enum type | PascalCase | `SoftRad`, `FitData`, `enum class Sample` |
| enum value | PascalCase | `Sample::ZJet`, `CorLevel::L1L2Res` |
| function, method | camelCase, starting with a verb | `run()`, `readGraphs()`, `getResponse()` |
| variable, parameter | camelCase | `ptMin`, `etaMax`, `refIov` |
| data member | camelCase + trailing `_` (CMSSW style) | `config_`, `graphs_` |
| compile-time constant | `k` + PascalCase | `kMaxSources` |
| namespace | camelCase, short | `graphTools`, `hdm` |
| file | the class name | `SoftRad.h`, `SoftRad.cpp`, `SoftRad.json` |
| JSON key | camelCase | `"refIov"`, `"ptRange"`, `"alphaMax"` |
| CLI step name | camelCase | `reprocess`, `jecBands`, `softRad`, `syst`, `renormPf`, `fit`, `plot` |
| output file | the step name | `forfit/.../softRad.root` |
| preprocessor macro | avoid; `UPPER_CASE` if unavoidable | |

**Acronyms are written as words:** `Jec`, `Mpf`, `Hdm`, `Fsr`, `Iov`, `Pf`,
`Rc`, as in `refIov`, `mpfChs`, `JecBands`, `useHdm`. The only exceptions are
the proper names `L1L2Res` and `L1L2L3Res`.

**Not renamed, on purpose:**

* **ROOT object names inside files** (`ratio/eta00-13/mpfchs1_zjet_a100`,
  `fsr/hkfsr3_*`, `sys/emat`). They are the data contract, the old notes refer
  to them, and the parity check matches on them.
* External input file names in `prefit/`.
* The vendored CMSSW JEC classes (`FactorizedJetCorrector`, ...). Their
  methods are already mostly camelCase, and keeping them as they are makes
  later syncs with CMSSW easy.
* ROOT's own API (`GetN()`, `SetPoint()`).

**Enforced by tools rather than by review.** `.clang-tidy` with
`readability-identifier-naming` encodes the table above, and `.clang-format`
fixes the layout. Both tools are installed here (`/usr/bin/clang-tidy`,
`/usr/bin/clang-format`). `make lint` and `make format` run them.

### 3.1 Rename table (representative; the full table is produced while porting)

| today | new |
|---|---|
| `reprocess(epoch)` | `Reprocess::run(ctx)` |
| `softrad3(etamin, etamax, dodijet, epoch)` | `SoftRad::run(ctx)`; η ranges in `SoftRad.json`; `dodijet` dropped (unused) |
| `globalFitSyst(run)` | `GlobalFitSyst::run(ctx)` |
| `globalFitRenormPF(s)`, `globalFitPrecombinePF`, `globalFitPrecombineHDM` | `GlobalFitRenormPf::run`, `precombinePf`, `precombineHdm` |
| `globalFitL3Res(etamin, etamax, epoch, selectSample, selectMethods)` | `GlobalFitL3Res::run(ctx)` |
| `jesFitter(npar, grad, chi2, par, flag)` | `Chi2Function::operator()(par)` |
| `jesFit(x, p)` | `JesModel::evaluate(pt, par)` |
| `fitError(xx, pp)` | `FitResult::jesUncertainty(pt)` |
| `origJES`, `newJES` | `FitPlotter::origJes`, `newJes` |
| `setToyShapeFuncs()` | `JesModel::buildShapes()` (`setFullShapeFuncs` dropped) |
| `getFJC(l1, l2, res, path)`, `getJEC(jec, eta, pt, rho)`, `setEtaPtRho`, `funcCorrPt` | `JecLoader::makeCorrector`, `getJec`, `setEtaPtRho`, `corrPtFunc` |
| `getY(pt, ptmin, ptmax, g, ey)`, `getEntry(...)` | `SoftRad::findPointInRange`, `SoftRad::readEntry` |
| `tools::diffGraphs`, `ratioGraphs`, `mergeGraphs` | `graphTools::diffGraphs`, `ratioGraphs`, `mergeGraphs` (the only `tools::` functions the chain calls) |
| `tools::delta_phi`, `make_vector`, `GetPoint`, `Divide`, `Hadd`, ... | dropped (unused); if needed: `deltaPhi`, `getPoint`, `divideHists`, `addHists` |
| `Flavor::getResp`, `getFracs`, `loadJEC`, `loadRES` | `Flavor::getResponse`, `getFractions`, `loadJec`, `loadResiduals` |
| `Flavor::_mjec`, `_mhw7`, `_mres`, `doRobin` | `pythiaJec_`, `herwigJec_`, `residuals_`, `robinStyle_` |
| `setTDRStyle`, `tdrCanvas`, `tdrDiCanvas`, `tdrDraw`, `tdrLeg`, `tdrHist` | `TdrStyle::apply`, `makeCanvas`, `makeRatioCanvas`, `draw`, `makeLegend`, `makeFrame` |
| **globals → config fields** | |
| `CorLevel` (global string) | `Epoch::corLevel` (`enum class CorLevel`) |
| `isUL16`, `isAPV`, `isUL17`, `isUL18`, `isRun2` | removed (era overrides in config) |
| `fzeeptmin`, `fzeeptmax` | `ZllJetReader.json`: `"zee": {"ptRange": [15, 800]}` |
| `correctZmmMass`, `correctGamMass`, `valueGamScale` | `GraphCorrector.json`: `zmmMass.apply`, `gamMass.apply`, `gamScale.value` |
| `multijetMode` and its copy `multijetModeS` | `MultijetReader.json`: `"mode": "ptave"`; downstream reads it from `_meta/` |
| `gRhoDT`, `gRhoMC`, `gRho` | `JecBands.json`: `"rho": {"data": 21.39, "mc": 20.60, "average": 21.00}` |
| `rp_debug`, `verboseGF` | `-v` (`Logger` level) |
| `dofsr`, `dofsr3zj`, `dofsr3gj`, `dofsr3mj` | `FitData.json`: `"fsr": {"correct": true, "samples": ["zjet", "gamjet", "multijet"]}` |
| `fitPF_delta`, `minPFpt`, `maxPFpt` | `FitData.json`: `"pfTerms": {"dijet": {"fit": true, "delta": 0.20, "ptRange": [43, 600]}}` |
| `ptreco_gjet`, `alphaeffmax` | `FitData.json`: `ptRecoMin.gamjet`, `alphaEffMax` |
| `useP0Trk` ... `useP8Flv`, `penalizeFitPars`, `njesFit` | `JesModel.json`: `"parameters": [{"name": "p0Trk", "use": true, "penalized": true}, ...]` |
| `_paper`, `_useZoom`, `shiftPtBal`, `_cleanUncert` | `FitPlotter.json`: `paperStyle`, `zoom`, `shiftPtBal`, `maxPlotUncert` |
| `_vdt1`, `_vdt3`, `_vpt1`, `_vsrc`, `_vpf*`, `_mpf` | `FitData::graphs_`, `rawGraphs_`, `recoilPt_`, `sources_`, `pfFractions_`, `pfVariations_` |
| `_vdt2`, `_vpt2`, `_vpt3` | `FitResult::shiftedGraphs`, `shiftedRecoilDown`, `shiftedRecoilUp` (computed after the fit) |
| `cnt`, `Nk` | `FitMinimizer::nCalls_`, `Chi2Breakdown::nPoints` |

---

## 4. Class map

35 classes and 3 utility namespaces, plus the vendored JEC classes, in a flat `header/` and
`cpp/`. A † marks a class that has its own `config/<Class>.json`.

### 4.1 Core

| class | role | replaces |
|---|---|---|
| `CommandLine` | parses `argv` and prints `-h` (the epoch list comes from `Inputs`) | `runMe.sh`, `mk_reprocess*.C` |
| `Pipeline` † | orders epochs by `refIov`, picks steps (`--from`, `--with-deps`, `--check`) and runs them | `mk_reprocess.C` |
| `Inputs` † (`Inputs.json`) | epoch registry, root paths (`prefit`, `jercProtoLab`, `forfit`, `postfit`) and `${name}` path expansion; holds `Epoch` (name, era, refIov, corLevel, channel inputs, JEC tags) | 136 `new TFile` sites, `isUL*` flags |
| `ConfigFile` | loads a JSON file, applies `default` → `eras` → `epochs` overrides (`json::merge_patch`), throws on unknown keys and keeps the resolved JSON for provenance | — |
| `RunContext` | what one step run sees: `Epoch`, `Inputs`, `Logger`, output paths, git hash | globals `_epoch`, `cep` |
| `Logger` | log levels and a per-epoch `run.log` | `cout` + `rp_debug`, 40k `REMOVE POINT` lines |
| `JecSysError` | exception carrying context | `assert` |
| `InputFile` | read-only TFile; `get<T>(path)` throws with file and path, `find<T>(path)` returns an empty optional; returned objects are owned clones | `(TH1D*)f->Get(...); assert(h);` |
| `OutputFile` | writes `x.root.tmp`, creates directories, `write(path, obj)`, `commit()` renames; without `commit()` the temporary file is deleted | UPDATE / RECREATE of `jecdata<epoch>.root` |
| `GraphKey` | the naming contract as a value type: `Dir`, `EtaBin`, `Method`, `Sample`, `alpha` → `path()`, and `parse(string)` | ~300 hand-written `Form(...)` names |
| `Step` | interface: `name()`, `inputFiles(ctx)`, `outputFile(ctx)`, `run(ctx)` | — |

### 4.2 Steps (each implements `Step`)

| class | CLI step | output | replaces |
|---|---|---|---|
| `Reprocess` † | `reprocess` | `forfit/.../reprocess.root` | `reprocess.C` minus §3.8 |
| `JecBands` † | `jecBands` | `jecBands.root` | `reprocess.C` §3.8 (JEC central values, bands, L1 systematics) |
| `SoftRad` † | `softRad` | `softRad.root` | `softrad3.C` (both η ranges in one run) |
| `GlobalFitSyst` † | `syst` | `syst.root` | `globalFitSyst.C` |
| `GlobalFitRenormPf` † | `renormPf` (optional, not in `all`) | `renormPf.root` | `globalFitRenormPF.C` |
| `GlobalFitL3Res` † | `fit` | `postfit/.../fit.root`, `result.json` | `globalFitL3Res.C` lines 354–2600 |
| `FitPlotter` † | `plot` | `postfit/.../plots/*.pdf` | `globalFitL3Res.C` plotting, `pdf/<epoch>/` |

### 4.3 Reprocess helpers

| class | role |
|---|---|
| `ChannelReader` | interface: `read(const ChannelInput&, const RunContext&)` returns the channel's harmonised graphs keyed by `GraphKey` |
| `ZllJetReader` † | KIT Z+jet combinations (zeejet, zmmjet, and their merge zlljet) |
| `ZJetReader` † | Helsinki `jme_bplusZ` files (zjet) |
| `GamJetReader` † | `GamHistosRatio` files (gamjet) |
| `MultijetReader` † | multijet / dijet files, recoil, `mode` |
| `IncJetReader` † | `drawDeltaJEC` files (incjet) |
| `PfJetReader` † | `output-DATA/MC-2b` PF composition (data − MC) |
| `HadWReader` † | hadronic W files |
| `GraphCleaner` † | maximum error, minimum events, snipe list (epoch, sample, pT) |
| `GraphCorrector` † | Z / γ mass and scale corrections, multijet recoil scale |
| `JecLoader` | builds `FactorizedJetCorrector` / `JetCorrectionUncertainty` from tags in `Inputs.json`, caches them, `getJec(eta, pt, rho)` |
| `Flavor` † | flavour responses (existing class, refreshed); `SoftRad` uses it for gluon JES |

### 4.4 Fit

| class | role |
|---|---|
| `FitData` † | everything the χ² reads: response graphs (sample × method), recoil pT, PF fractions, and `NuisanceSource{name, shape, appliesTo}`; built by `FitData::load(config, ctx)`. Samples, methods, α, FSR, PF terms and the **explicit source list** are in `FitData.json` |
| `JesModel` † | the 9 parameters (name, start value, step, penalty) and their shape functions; `evaluate(pt, par)` |
| `Chi2Function` | `double operator()(const double* par) const`; `breakdown(par)` gives χ² and point counts per dataset |
| `FitMinimizer` † | TFitter / TMinuit (`MINI`, tolerance, strategy) behind one method `minimize(chi2, model)`; Minuit2 later behind the same interface |
| `FitResult` | parameters, covariance, eigenvectors, χ² breakdown, shifted graphs; `writeRoot(OutputFile&)`, `writeJson(path)`, `jesUncertainty(pt)` |

### 4.5 Utilities (namespaces, no state)

| file | contents |
|---|---|
| `GraphTools` | `graphTools::diffGraphs`, `ratioGraphs`, `mergeGraphs`, `multiplyGraph` |
| `HdmSolver` | `hdm::` the HDM / FSR solvers from `softrad3.C` as pure functions |
| `TdrStyle` | CMS plot style (old `tdrstyle_mod15.C`), no globals in the header |

### 4.6 Vendored (flat as well, contents unchanged)

`FactorizedJetCorrector`, `JetCorrectorParameters`, `SimpleJetCorrector`,
`JetCorrectionUncertainty`, `SimpleJetCorrectionUncertainty`: `.h` files go
to `header/` and `.cc` files to `cpp/*.cpp`. `Utilities.cc` is already a
header (include guard and anonymous namespace), so it becomes
`header/JetMetUtilities.h`. The only edit is rewriting the
`#include "CondFormats/JetMETObjects/..."` lines. Each file gets a one-line
banner `// vendored from CMSSW CondFormats/JetMETObjects, do not edit`, and
`.clang-tidy` / `.clang-format` skip them.

### 4.7 Runtime picture

```
main.cpp ─ CommandLine ─ Pipeline ─┬─ Inputs (Inputs.json) ─ Epoch[]
                                   │
           for each epoch (refIov first), for each step:
                                   │
                                   ▼
                     Step::run(RunContext)     InputFile(s) ──▶ OutputFile (tmp → commit)
   ┌───────────────┬───────────────┼───────────────┬────────────────────┬──────────────┐
Reprocess        JecBands        SoftRad        GlobalFitSyst      GlobalFitL3Res     FitPlotter
 ├ ChannelReader[7]  └ JecLoader     ├ hdm::          (GlobalFitRenormPf)  ├ FitData           └ TdrStyle
 ├ GraphCleaner        └ vendored     └ Flavor                              ├ JesModel
 └ GraphCorrector         JEC                                               ├ Chi2Function
                                                                            ├ FitMinimizer
                                                                            └ FitResult
```

---

## 5. Key interfaces (sketches)

```cpp
// header/Step.h
class Step {
 public:
  virtual ~Step() = default;
  virtual std::string name() const = 0;                       // "softRad"
  virtual std::vector<std::string> inputFiles(const RunContext& ctx) const = 0;
  virtual std::string outputFile(const RunContext& ctx) const = 0;
  virtual void run(const RunContext& ctx) = 0;                // throws JecSysError
};
```

```cpp
// header/SoftRad.h — the pattern every configurable class follows
struct SoftRadConfig {
  std::vector<EtaRange> etaRanges;        // {0,1.3}, {0,2.5}
  double ru = 0, rn = 0;                  // with ruUp/ruDown, rnUp/rnDown
  std::map<Sample, double> refAlpha;
  bool useGluonJes = false;
  static SoftRadConfig fromJson(const ConfigFile& cfg);   // validates, throws
};

class SoftRad : public Step {
 public:
  SoftRad(SoftRadConfig config, const Flavor& flavor);
  std::string name() const override { return "softRad"; }
  ...
 private:
  const SoftRadConfig config_;
  const Flavor& flavor_;
};
```

```cpp
// header/OutputFile.h
class OutputFile {
 public:
  explicit OutputFile(std::string finalPath);   // opens finalPath + ".tmp"
  ~OutputFile();                                // removes .tmp unless committed
  void write(const std::string& path, const TObject& obj);   // creates dirs
  void commit();                                // Write, Close, rename
};
```

```cpp
// header/GraphKey.h
enum class Dir { Data, Mc, Ratio };
enum class Sample { ZJet, ZeeJet, ZmmJet, ZllJet, GamJet, Multijet, IncJet, PfJet, HadW };
struct GraphKey {
  Dir dir; EtaBin eta; Method method; Sample sample; int alpha;  // alpha in %
  std::string path() const;               // "ratio/eta00-13/mpfchs1_zjet_a100"
  static GraphKey parse(const std::string& path);   // throws on unknown names
};
```

```cpp
// header/Chi2Function.h, header/FitMinimizer.h
class Chi2Function {
 public:
  Chi2Function(const FitData& data, const JesModel& model);
  double operator()(const double* par) const;
  Chi2Breakdown breakdown(const double* par) const;
 private:
  const FitData& data_;
  const JesModel& model_;
};

class FitMinimizer {
 public:
  explicit FitMinimizer(FitMinimizerConfig config);
  FitResult minimize(const Chi2Function& chi2, const JesModel& model, const FitData& data);
 private:
  // TFitter::SetFCN needs a C function. The only static pointer in the code;
  // set on entry to minimize(), reset on exit. Removed when moving to Minuit2.
  static const Chi2Function* active_;
  static void fcn(int& nPar, double* grad, double& chi2, double* par, int flag);
  const FitMinimizerConfig config_;
};
```

---

## 6. Config files (one per configurable class)

```
config/
  Inputs.json          epochs: era, refIov, corLevel, channel files + reader, JEC tags; roots  (runFit -h)
  Pipeline.json        step order, which steps `all` runs (renormPf off)
  Reprocess.json       output axes: dirs × types × samples × η × α, skip rules as data
  ZllJetReader.json  ZJetReader.json  GamJetReader.json  MultijetReader.json
  IncJetReader.json  PfJetReader.json  HadWReader.json          native names, pT windows
  GraphCleaner.json    GraphCorrector.json
  JecBands.json        Flavor.json
  SoftRad.json         GlobalFitSyst.json   GlobalFitRenormPf.json
  GlobalFitL3Res.json  FitData.json   JesModel.json   FitMinimizer.json   FitPlotter.json
```

Every file has the same shape, so `ConfigFile` handles all of them the same
way:

```json
{
  "_doc": "SoftRad: HDM / FSR corrections (was softrad3.C)",
  "default": {
    "etaRanges": [[0.0, 1.3], [0.0, 2.5]],
    "ru": 0.685, "_ruSource": "...",
    "useGluonJes": false, "_useGluonJesNote": "DP_2021 setting"
  },
  "eras":   { "UL16APV": { }, "UL16": { }, "UL17": { }, "UL18": { } },
  "epochs": { "2016GH": { } }
}
```

**Paths and `JERCProtoLab`.** `Inputs.json` names the roots once, and file
entries refer to them:

```json
"roots": { "prefit": "prefit", "jercProtoLab": "../JERCProtoLab",
           "forfit": "forfit", "postfit": "postfit" },
...
"zeejet": { "reader": "ZllJetReader",
            "file": "${jercProtoLab}/Summer19UL16/L3Residual_Z/APV/JEC_Combination_Zee/ZJetCombination_Zee_DYJets_amc_21Feb2020_Summer19UL16_V7_L1L2L3Res.root",
            "subdir": "Run2016preVFPBCDEFearly" }
```

A root can be an EOS path (`root://eosuser.cern.ch//eos/...`), because
`TFile::Open` accepts it. `--check` verifies every expanded path before
anything runs.

**`CorLevel` (kept).** Each epoch has a default `corLevel`, and
`--corLevel L1L2Res` overrides it. Files that differ by level are given per
level:

```json
"multijet": { "reader": "MultijetReader",
              "file": { "L1L2L3Res": "${jercProtoLab}/.../multijet_UL2016BCDEF_jecV7_jerV2_closure.root",
                        "L1L2Res":   "${jercProtoLab}/.../multijet_UL2016BCDEF_jecV5_jerV2.root" } }
```

Outputs go to `forfit/<corLevel>/<epoch>/` and `postfit/<corLevel>/<epoch>/`,
so the two levels never overwrite each other. A refIOV is always looked up at
the same level.

**Caveat (probe on 2026-11-18):** with `CorLevel = "L1L2Res"`, today's chain
stops at a missing input for every epoch, even when it reads all of jecsys and
`../JERCProtoLab` (details in `toDo.txt` §C). We can port the L1L2Res code
path and its config, but we have no reference output to validate it against
until those inputs are found. Until then, `runFit` warns that the path has not
been validated.

---

## 7. What this changes in `refact.md`

* Layout: flat `header/` and `cpp/`, no `external/`; the vendored JEC code
  is flat too (§4.6). `nlohmann/json.hpp` comes from the system or LCG view
  (`/usr/include/nlohmann`), with `JSON_INC` overridable in the Makefile.
* Config: one JSON file per class (§6) instead of five files grouped by
  concern.
* `prefit/` holds only our copies; `JERCProtoLab` is referenced by path.
* Output paths gain a `<corLevel>/` level.
* Migration:
  * **Phase 1 stays a pure move with `git mv`** (no renames), so `git log
    --follow` keeps the history.
  * camelCase is applied to each module when it is ported to its class
    (Phases 2–5).
  * Phase 6 sweeps what is left, and `make lint` (clang-tidy naming) becomes
    part of `make test`.

---

## 8. Open questions for round 2

1. **Member suffix:** `config_` (CMSSW style, my recommendation) or `mConfig`?
2. **File and class names:** PascalCase files matching the class
   (`SoftRad.h`, `config/SoftRad.json`) [recommended]. The alternative is
   camelCase files (`softRad.h`).
3. **Step class names:** keep the old macro names (`GlobalFitSyst`,
   `GlobalFitL3Res`, `GlobalFitRenormPf`) so jecsys users find them
   [recommended], or use shorter names (`Syst`, `Fit`, `RenormPf`)?
4. **Seven channel readers, one JSON each:** fine, or merge the reader
   settings into `Reprocess.json`? I recommend separate files, since that
   follows your same-name rule, and each file stays small.
5. **`JecBands` as its own step:** OK? It needs no channel inputs, so it can
   run, and be checked, on its own.
6. **`--corLevel` layout:** `forfit/<corLevel>/<epoch>/` OK?
7. **`.clang-format` style:** base it on `Google` with 2-space indent and a
   100-column limit, close to the current code? Or another style you use?
