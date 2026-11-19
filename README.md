# JecSys

Run 2 UL L3Res global fit of the CMS jet energy corrections, for the epochs 2016BCDEF,
2016GH, 2017BCDEF and 2018ABCD. It is a cleaned-up extract of [jecsys](../jecsys).

The package has two halves, and the directories follow the data:

```
input/prefit/     ──►  input/forfit/                   ──►  output/postfit/
external inputs        reprocess/: pre-fit files            fit results, plots
(read only)            card/: datacards, workspaces         made by runFit
                       made by input/jecMakeInputs and by runFit
```

| half | what it does | how it is written |
|---|---|---|
| **input creation** (`input/`) | merges the inputs of the different groups, derives the FSR corrections and the systematic sources; writes `input/forfit/reprocess/jecdata<epoch>.root` | compiled C++ in `input/` (`header/`, `cpp/`, `bin/`); settings in `input/config/` |
| **fit** (top level) | turns that file into a datacard, fits it, writes results and plots | compiled C++ in `header/`, `cpp/`, `bin/`; settings in `config/` |

The fit half is organised like the Higgs
[combine](https://github.com/cms-analysis/HiggsAnalysis-CombinedLimit/) tool:
**datacard → workspace → fit methods**. It replaced the macro `globalFitL3Res.C` and
gives identical results (see [Tests](#tests)).

## Run

```bash
source runMe.sh       # everything: make -C input && make && ./runFit -e all
```

or step by step:

```bash
make inputs           # input creation for 2016GH, 2017BCDEF, 2018ABCD   (= make -C input)
make -C input 2016GH  # ... or one epoch: writes input/forfit/reprocess/jecdata2016GH.root
make -C input test    # tests of the input creation
make                  # builds the fit half
./runFit -h           # options, and the epochs of config/Inputs.json
./runFit -e 2016GH    # card -> workspace -> fit -> plots
./runFit -e all       # all epochs, reference IOVs first
```

Requirements: ROOT 6 with Minuit, a C++17 compiler, `nlohmann/json.hpp` (set
`JSON_INC=<dir>` for `make` if it is not in a default include path). JecSys must sit
next to `JERCProtoLab`, because the input creation reads some inputs from it.

Only code and configuration are in git. Everything in `input/prefit/`, and the `.root`
and `.txt` files in general, are ignored and must be copied in (see `input/README.md`).

## Input creation (`input/prefit/` → `input/forfit/reprocess/`)

**See [`input/README.md`](input/README.md).** In short: `input/jecMakeInputs` merges
the results of the analysis groups (multijet, γ+jet, Z+jet, hadW, inclusive and PF jets)
into one pre-fit file per epoch, with the FSR (HDM) corrections, the systematic sources
and the JEC reference bands. It is a stage of its own (`input/Makefile`,
`input/config/`, `input/header/`, `input/cpp/`, `input/bin/`, `input/test/`) that shares
only the common classes and `config/Inputs.json` with the fit. It replaced the jecsys
ROOT macros (`reprocess.C`, `softrad3.C`, ...) and gives byte-identical output.

## Fit half

`runFit` runs four steps. Each is also an executable of its own.

| step | command | reads | writes |
|---|---|---|---|
| card | `./jecMakeCard -e 2016GH` | `input/forfit/reprocess/jecdata2016GH.root`, `config/MakeCard.json` | `input/forfit/card/L1L2L3Res/2016GH/datacard.txt`, `shapes.root` |
| workspace | `./jecWorkspace input/forfit/card/L1L2L3Res/2016GH/datacard.txt` | the card, `config/JesModel9.json` | `input/forfit/card/L1L2L3Res/2016GH/workspace.root` |
| fit | `./jecFit input/forfit/card/L1L2L3Res/2016GH/workspace.root` | the workspace | `output/postfit/L1L2L3Res/2016GH/fit.root`, `result.json` |
| plot | `./jecPlotFit output/postfit/L1L2L3Res/2016GH/fit.root` | `fit.root` | `fit_prefit.pdf`, `fit_postfit.pdf`, `fit_pf.pdf`, `fit_pulls.pdf` |

* **Datacard** (`doc/datacard.md`): a text table of channels × nuisances. A channel is
  one graph, e.g. `ptchs_gamjet` or `chf_pfjet`. It is generated; edit
  `config/MakeCard.json`, not the card. `shapes.root` holds the graphs as they enter
  the fit (FSR correction applied) and the nuisance histograms.
* **Workspace**: one self-contained file with the card, every object it refers to and
  the physics model. It can be archived or sent to someone to reproduce a fit.
* **Physics model** `JesModel9`: the 9-parameter detector and fragmentation model.
  Parameters, start values and shape constants are in `config/JesModel9.json`.
* **Reference IOV**: 2016GH takes four hadW nuisances from 2016BCDEF. Its card says so
  (`shapes hadw_... ../2016BCDEF/shapes.root`), and `jecMakeCard -e 2016GH` fails with a
  message if that card does not exist. `-e all` does the reference IOVs first.

### The fit

```
χ² = Σ_channels Σ_points [(data + Σ_s θ_s·shift_s(pT) − model(pT; p)) / σ]²
   + Σ_s θ_s²        one unit Gaussian constraint per nuisance
   + Σ_k p_k²        penalty on the model parameters
```

Three kinds of channel (`kind` row of the card):

| kind | what is compared |
|---|---|
| `response` | data with the model divided by the closure reference |
| `multijet` | as `response`, after multiplying the data by the model at the recoil pT |
| `pfFraction` | a PF energy fraction with the model's prediction, with a dead zone `delta` |

It is minimised with TMinuit (`MINI`).

### Fit methods and cross-checks

```bash
W=input/forfit/card/L1L2L3Res/2016GH/workspace.root
./jecFit $W                                                             # FitDiagnostics (default)
./jecFit $W --mask '.*_hadw' -n noHadW                                  # leave channels out
./jecFit $W --setParameters p8Flv=0 --freezeParameters p8Flv -n noFlv   # fix a parameter
./jecFit $W -M GoodnessOfFit -t 200                                     # χ² per channel, p-value from toys
./jecFit $W -M Impacts                                                  # effect of each nuisance on the JES
./jecFit $W -M MultiDimFit -P p0Trk                                     # χ² scan of a parameter
```

`--mask` and `--freezeParameters` take regular expressions; a pattern that matches
nothing is an error. `-n <label>` is added to the output file names, so a cross-check
does not overwrite the nominal result. The same options work with `runFit`.

Several cards can be fitted together:

```bash
./jecCombineCards 2016BCDEF=input/forfit/card/L1L2L3Res/2016BCDEF/datacard.txt \
                  2016GH=input/forfit/card/L1L2L3Res/2016GH/datacard.txt \
                  --decorrelate 'fsr_.*' -o input/forfit/card/L1L2L3Res/Run2016/datacard.txt
```

Nuisances with the same name become one parameter; `--decorrelate` keeps the matching
ones separate. The model parameters are shared by all channels, which is not what a
combined Run 2 fit needs yet (`toDo.txt` F3).

### Results

`output/postfit/<corLevel>/<epoch>/fit.root` (plots in `plots/` next to it):

| path | content |
|---|---|
| `ratio/eta00-13/sys/` | `emat`, `vpar`, `verr`, `h2emat`, `h2cov`, `h1par`, `hjesfit`, `hjesfit2`, `hjesfit_eig<i>`, `hjesfit_par<i>`, `eigvec`, `eigmat` |
| `ratio/eta00-13/fsr/` | `hkfsr2_<channel>`, `hkfsr2_<channel>_eig<k>`: refitted FSR corrections |
| `prefit/`, `postfit/`, `model/` | graphs per channel, for plotting |
| `_meta/fit/` | the result as JSON, the datacard |

The objects under `ratio/eta00-13/` have the names `globalFitL3Res.C` used when it
wrote them into `jecdata<epoch>.root`. The fit no longer modifies that file, so a tool
that read the fit result from `jecdata<epoch>.root` has to read `fit.root` instead.
`result.json` has the χ², the parameters and the nuisances in readable form.

## Layout

```
Makefile                 the fit stage: obj/libjecsys.a and one executable per file in bin/; "make inputs", "make test"
runMe.sh                 the whole chain
bin/                     runFit, jecMakeCard, jecWorkspace, jecFit, jecCombineCards, jecPlotFit, jecCompare
header/  cpp/            the classes of the fit half and the shared ones, flat, one class per file pair
config/                  Inputs.json (epochs, roots; shared), MakeCard.json (channels, nuisances), JesModel9.json (model)
test/                    unit tests, parity test
doc/                     datacard.md; implementation notes prompt_minicombine_phase_*.txt
toDo.txt                 open items
input/                   input creation, a stage of its own (input/README.md): Makefile, config/,
                         header/ cpp/ bin/, obj/, test/
input/prefit/            external inputs                                   (not in git)
input/forfit/            reprocess/jecdata<epoch>.root, card/              (not in git)
output/postfit/          fit results and plots                             (not in git)
obj/                     build products                                    (not in git)
```

Main classes of the fit half:

| class | role |
|---|---|
| `Inputs` | epoch registry (`config/Inputs.json`); the roots (`{prefit}`, ...); builds every output path |
| `MakeCard` | pre-fit file → datacard + shapes |
| `Datacard` | read, write, validate and combine cards |
| `Workspace` | the frozen fit problem |
| `PhysicsModel`, `JesModel9`, `ShapeFunction` | the model and its functional forms |
| `Channel` | one data set and its χ² rule (response, multijet, PF fraction) |
| `Chi2Function` | the χ²; const, no global state |
| `FitMinimizer` | TMinuit |
| `FitMethod` | `FitDiagnostics`, `GoodnessOfFit`, `Impacts`, `MultiDimFit` |
| `FitPlotter` | plots from `fit.root` |
| `InputFile`, `OutputFile` | ROOT files; outputs are written to `x.tmp` and renamed on success |
| `ConfigFile` | JSON with `default` ← `eras` ← `epochs` overrides; unknown keys are errors |

Conventions (both stages):
* One main class per file pair, named after the class (`header/MakeCard.h`,
  `cpp/MakeCard.cpp`), and one configuration file per configurable class, with the same
  name (`config/MakeCard.json`, `input/config/SoftRad.json`), read only by that class.
* `config/Inputs.json` says *what* to read for an epoch; `<Class>.json` says *how*. The
  code never compares epoch or era names: differences between years are era and epoch
  overrides in the JSON. Nothing is configured by editing code.
* camelCase for functions, variables and JSON keys, PascalCase for classes and files,
  trailing `_` for data members, acronyms written as words (`JecBands`, `refIov`).
* ROOT object names inside the files (`ratio/eta00-13/mpfchs1_zjet_a100`, `sys/emat`)
  keep their jecsys names: they are the data contract between the stages and with
  older notes. So do the vendored CMS JEC classes and the external input files.
* Dependencies are passed in constructors; no singletons. Errors are thrown as
  `JecSysError` with context (file, object, epoch) and end the program with a message
  and exit code 1.
* Comments such as "was globalFitL3Res.C:1308" or "as reprocess.C" give the origin of a
  number or a behaviour in the removed macros (see the git history or jecsys).

### Changing things

| to | edit |
|---|---|
| add or change an epoch | `config/Inputs.json`, and the era/epoch blocks of the configs |
| change the input creation (files, cuts, JEC tags, ...) | `input/config/<Class>.json`; see `input/README.md` |
| change samples, methods, a scale uncertainty, a PF pT range | `config/MakeCard.json` (era and epoch overrides in the same file) |
| change a model start value or shape constant | `config/JesModel9.json` |
| add a physics model | a class implementing `PhysicsModel`, registered in `makePhysicsModel` |
| add a kind of channel | a class implementing `Channel`, plus its name in `ChannelKind` |
| add a fit method | a class implementing `FitMethod`, registered in `makeFitMethod` |

## Tests

```bash
make test             # the fit
make -C input test    # the input creation (input/README.md)
```

`make test` runs the unit tests (`test/test*.cpp`, no input files needed) and the parity
test (`test/parity.sh`). The parity test fits the four epochs from frozen pre-fit files and
compares the result with what `globalFitL3Res.C` produced from the same files: the
covariance matrix, the 36 parameters and their errors, `hjesfit` with its eigenvectors,
and the refitted FSR histograms. They agree exactly. It needs the golden files in
`test/golden/`, which are not in git; see `test/README.md`.

## Known limits

Listed in `toDo.txt`. The main ones:

* The input creation writes all its steps into one shared `jecdata<epoch>.root`, updated
  in place (`toDo.txt` F8).
* `jecMakeCard` supports the samples of the four reference epochs (multijet, γ+jet,
  Z+jet, hadW, PF fractions), not inclusive jets or dijets.
* Combined fits share the model parameters between epochs.
* The plots are new and simple; the plots of `globalFitL3Res.C` were not ported.
