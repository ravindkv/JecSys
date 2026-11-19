# Input creation (`input/prefit/` → `input/forfit/reprocess/`)

The input creation makes the pre-fit file `input/forfit/reprocess/jecdata<epoch>.root`
of each epoch from the results of the analysis groups in `input/prefit/` (and
`../JERCProtoLab`). It merges their graphs, derives the FSR corrections with the HT
decomposition method (HDM), the systematic sources and the JEC reference bands. The fit
(`../README.md`) reads that file.

It is a stage of its own, separate from the fit: its own Makefile, classes, executable,
settings, build products and tests, all below `input/`. With the fit it shares only the
common classes of the top level (`ConfigFile`, `Inputs`, `TextTools`, `CommandLine`,
`JecSysError`) and the epoch registry `config/Inputs.json`.

It used to be the ROOT macros of jecsys (`reprocess.C`, `softrad3.C`, `globalFitSyst.C`,
`globalFitRenormPF.C`, `mk_reprocess.C`, with `tools.C` and `tdrstyle_mod15.C`). On
2026-11-19 they were replaced, by way of a short-lived Python port, by the C++ classes
below. The output is byte-identical to that of the macros for the four epochs. The
macros are in the git history.

## Run

From the repository root (every path in the settings is relative to it):

```bash
make inputs                  # the epochs 2016GH, 2017BCDEF, 2018ABCD (= make -C input; -j runs them in parallel)
make -C input 2016BCDEF      # one epoch
make -C input test           # unit tests + byte parity with the golden snapshots
input/jecMakeInputs -h       # options
input/jecMakeInputs -e all --outdir /tmp/x --snapshot /tmp/snap   # elsewhere, plus a copy after every step
input/jecMakeInputs -e 2016GH --steps softRad,globalFitSyst --file F.root      # some steps on a given file
```

`input/forfit/reprocess/jecdata2016BCDEF.root` is shipped, and 2016BCDEF is not in the
default list of epochs. `make -C input 2016BCDEF` remakes it, without the 65 objects of
the old fit under `ratio/eta00-13/sys/` that the shipped file also has (the fit does not
need them).

## Layout

```
input/
├── Makefile           make [<epoch> | lib | bin | test | clean]
├── config/            <Class>.json: the settings of each class
├── header/  cpp/      the classes, flat (header/XYZ.h, cpp/XYZ.cpp); also the standalone CMS
│                      JEC classes CondFormats/JetMETObjects (interface/ in header/, src/ in
│                      cpp/; third-party, not edited) and prelude.h (see Build)
├── bin/               jecMakeInputs.cpp, built as input/jecMakeInputs
├── obj/               build products: libjecinput.a, libcondformats.a  (not in git)
├── test/              unit tests, parity.sh, README.md; golden/: the reference  (not in git)
├── prefit/            external inputs, read only  (not in git)
│   ├── DP2021033/  DP2021033V2/   input ROOT files of the DP-2021/033 set
│   ├── rootfiles/                 other input ROOT files, and flavor/ (flavour JEC text files)
│   └── jec/                       JEC text files (CondFormats/JetMETObjects/data in jecsys)
└── forfit/            outputs  (not in git)
    ├── reprocess/jecdata<epoch>.root   the pre-fit file of an epoch
    └── card/<corLevel>/<epoch>/        datacard, shapes and workspace (made by the fit)
```

In a jecsys checkout, `DP2021033*/` and `rootfiles/` have the same names, and
`prefit/jec/` is `CondFormats/JetMETObjects/data/`.
`prefit/rootfiles/hadWMC16GHV7_JEC.root` is a symlink to a stand-in made from
`jecdataRun2Test.root` (see `missing.md` in jecsys).

## The chain

`input/jecMakeInputs` runs the steps of `config/InputChain.json` for each epoch, in one
process, on the pre-fit file of the epoch:

| step | class (was) | settings | writes into the pre-fit file |
|---|---|---|---|
| `reprocess` | `Reprocess` (`reprocess.C`) | `Reprocess.json` | creates it (RECREATE): `<tier>/eta<..>/<type>_<sample>_a<alpha>`, the graphs of every sample, read, cleaned and corrected (uncleaned copies in `orig/`), with the event counts; `<tier>/eta00-13/mass_<sample>_a<30\|100>`, the Z mass histograms; `ratio/eta<..>/herr*`, `hjes`, `hl1*`, `hrc*`, `hsys*`, `hruncl`, the JEC reference bands and L1 systematics |
| `softRad` ×2 | `SoftRad` (`softrad3.C`) | `SoftRad.json` | `hdm_<method>_<sample>`, `fsr/hkfsr3_<method>_<sample>[<component>]`: FSR corrections, \|η\| < 1.3 and < 2.5 |
| `globalFitSyst` | `GlobalFitSyst` (`globalFitSyst.C`) | `GlobalFitSyst.json` | `ratio/eta00-13/sys/`: multijet JER, photon scale, hadW fitProb and FSR shapes; HDM variants |
| `globalFitRenormPF` | `GlobalFitRenormPF` (`globalFitRenormPF.C`) | `GlobalFitRenormPF.json` | renormalised PF fractions and pre-combined PF and HDM inputs (not used by the fit today) |

`reprocess` creates the file and the later steps update it in place, as the macros did.
A later step must therefore not run twice on the same file (it would add key cycles):
`--steps` without `reprocess` is for a fresh copy, a golden `1_reprocess.root` say.

| class | role |
|---|---|
| `InputChain` | the steps of `InputChain.json` (was `mk_reprocess.C`); builds every step first, so all settings are checked before a file is touched; timing, snapshots |
| `InputStep` | the step interface, and shared tools (opening files, typed settings access, ROOT constants such as `"kRed+1"`) |
| `ReprocessSettings` | the typed settings of `Reprocess.json` |
| `SampleTable` | the samples, the flavour sets (`@zjetFlavor` → `zi`, `zb`, ...) and the product table (which tier, eta bin, type and alpha exist per sample) |
| `GraphMaker` | part 1 of `reprocess`: one graph per tier, eta bin, type, sample and alpha |
| `JecBands` | part 3 of `reprocess`: the JEC reference bands, with `JecInverter` and `CorrectorCache` (one shared `FactorizedJetCorrector` per list of tags) |
| `GraphTools` | the graph helpers of `tools.C` (`diffGraphs`, `ratioGraphs`, `mergeGraphs`), bit-identical to them |
| `TdrStyle` | the CMS TDR plot style of `TdrStyle.json` (was `tdrstyle_mod15.C`): a typed table of `TStyle` setters |

## Settings

`config/<Class>.json` holds every number, name, path and table of a class; the code holds
the logic.

* The layout is that of the fit's configs: `default` ← `eras` ← `epochs` (JSON merge
  patch). A key of an era or epoch block that is not in `default` is an error (a typo).
  `null` in `default` means "set per era or epoch", and a `null` left after the merge is
  an error naming the epoch. Keys that start with `_` are notes.
* Every class parses its settings into a typed structure when it is built. A wrong key,
  type or value stops `jecMakeInputs` before any file is opened.
* Object names inside the input files are templates in Python `str.format` syntax, e.g.
  `{tierName}/eta_{eta1x10:02.0f}_{eta2x10:02.0f}/{name}_zmmjet_a{alpha100:1.0f}`
  (`textTools::formatTemplate`).
* Paths are relative to the repository root and start with a root of
  `config/Inputs.json`: `{prefit}` (`input/prefit`), `{protolab}` (`../JERCProtoLab`),
  `{forfit}` (`input/forfit/reprocess`).
* `config/Inputs.json` says *what* to read for an epoch (its era, its pre-fit file);
  `<Class>.json` says *how*. The code never compares epoch or era names: differences
  between years are overrides in the JSON.
* Scope: the four epochs of `config/Inputs.json` at `L1L2L3Res`. Branches of the macros
  that none of them used (Run2Test, 2017H, the sub-IOVs, `L1L2Res`, alternative modes)
  were not ported; the `_doc` of each JSON file lists them.

What needs a code change, and what does not:

| change | where |
|---|---|
| a new epoch or era, a new input file or object name | `config/Inputs.json` and the era/epoch blocks |
| JEC tags, luminosities, uncertainty sources | `Reprocess.json` (`jec`) |
| cuts, pT ranges, minimum events, maximum errors, points removed by hand | `Reprocess.json` (`cleaning`, `snipe`) |
| which sample × type × α combinations exist | `Reprocess.json` (`products`) |
| mass or recoil correction parameters and functional forms | `Reprocess.json` (`corrections`, TF1 formula strings) |
| marker styles, colours, the TDR style | `Reprocess.json` (`style`), `TdrStyle.json` |
| the HDM model constants and functions | `SoftRad.json` |
| order and arguments of the steps | `InputChain.json` |
| a new kind of graph operation, a new cleaning rule, a new band | C++ |
| a new step | a class implementing `InputStep`, its JSON, and a line in `InputChain.cpp` |

Names, paths, numbers, lists, tables and TF1 formula strings go in JSON; control flow,
band arithmetic and ROOT object handling stay in C++. JSON is not meant to become a
programming language.

## JEC inversion

The JEC bands of `reprocess` need about 100k JEC values at a *corrected* pT: the
correction c(x) at the raw pT x with x·c(x) = pT(corr), where c is
`FactorizedJetCorrector::getCorrection()` at (η, x, ρ, jet area). `jec.inversion` in
`Reprocess.json` (or `--jec-inversion`) chooses how x is found (`JecInverter`):

| mode | how | `reprocess` CPU, 2016GH | output |
|---|---|---|---|
| `getx` | `TF1::GetX` on [5, 6500] GeV for every call, as `reprocess.C` | 5.3 s | the reference |
| `memo` (default) | the same, but a repeated call (same corrector, η, pT, ρ) is answered from a cache | 3.4 s | identical |
| `secant` | a float-aware secant, with the cache and a `GetX` fallback | 1.0 s | not byte-identical |

Why `getx` is slow, and why the faster modes are safe:
* `GetX` scans [5, 6500] in 100 steps before it bisects towards 1e-10: ~48 evaluations of
  c per inversion. But c returns a `float` (6e-8 relative), so x·c(x) is a staircase
  and most of that precision is not real.
* Half of the inversions repeat (the two eta bands share their eta bins; equal tag
  lists appear under several keys). The cache keys are exact doubles: a repeated call
  computes its arguments with the same expressions, so it hits exactly. The shared
  correctors are safe because every evaluation sets η, pT, ρ and area again.
* The secant starts with two fixed-point steps from x = pT(corr) / c(pT(corr)), which
  land within ~1 %, then takes secant steps. It stops on a plateau of the staircase or
  at a step of 1e-9, and returns the c with the smallest residual (~6 evaluations per
  inversion). In ~2 % of the cases the root lies on the edge between two neighbouring
  float values of c, the secant bounces, and `GetX` decides after 30 steps. Returning
  the better side of the edge there instead would save 10 % of the time but double the
  differences, so it was not done.
* `secant` changes 28 of the 3886 objects of 2016GH (56 of 4558 for 2017BCDEF and
  2018ABCD), all JEC band histograms, by at most 4.4e-8 absolute: the float precision
  of the JEC. Either side of a step is a valid root.

## ROOT state

The classes reproduce the macros' use of ROOT's global state on purpose, because it
decides the bytes that are written. Changing any of it changes the output; do that as a
separate change, with new goldens.
* `TH1::AddDirectory` must be on (ROOT's default). `InputChain::run` turns it on again,
  because the shared `runMain()` turns it off for the fit. Otherwise new histograms
  lose a flag of their function list and the `TFile::Write()` below writes less.
* The current directory at every `Clone`, `ProjectionX/Y`, `ProfileX` and `new TH1`
  follows the macro: it decides which file owns the object, and a later `ProfileX` of
  the same name reuses it. The multijet event counts of the ratio tier are the data
  counts in memory, rebinned a second time.
* `GlobalFitRenormPF` ends each part with `TFile::Write()`, which writes the histograms
  it read from the file again (new key cycles).
* New histograms take their attributes from `gStyle`. `reprocess` is the first
  `setTDRStyle()` call of a fresh ROOT session (`TdrStyle::setAsFirst`, also when an
  earlier epoch ran in the same process). The later steps have the style of the second
  and later calls, whose `TStyle` takes the axis defaults of ROOT's last predefined
  style (`TdrStyle::setAsInChain`).
* `TH1::SetBinContent` counts entries, so the number and order of the calls matter.

## Tests

`make -C input test` runs the unit tests (`test/testGraphTools.cpp`,
`test/testInputSettings.cpp`; no input files needed) and `test/parity.sh`. The parity
test runs the whole chain for the four epochs in one process, from scratch, and compares
the file after every step with the golden snapshots in `test/golden/` (the output of the
macros) object by object, every key cycle, streamed bytes
(`../jecCompare --bytes --complete`). All are identical, with `memo` and with `getx`
(`INVERSION=getx`). The goldens are not in git and cannot be remade from the macros
any more: keep a copy (`test/README.md`).

## Build

* `input/Makefile` builds `obj/libjecinput.a` from `cpp/*.cpp`, `obj/libcondformats.a`
  from the CMS JEC classes, and `jecMakeInputs`. It includes the shared headers with
  `-I../header` and links `../obj/libjecsys.a`, which it builds with the top-level make.
* The CMS JEC classes compile with plain `g++` given two adjustments, without editing
  them: `header/prelude.h` is force-included (`#include <iostream>` and
  `using namespace std;`, which two of their files rely on and which cling provided), and
  `Utilities.cc` is left out because `JetCorrectorParameters.cc` includes it (hence
  `-Icpp`).
* Keep `-O2` and add neither `-ffast-math` nor `-march=native`: FMA contractions would
  change the last bits of the results, and the parity test compares bytes.

Performance (2026-11-19, shared machine): 4.5 to 6.5 CPU seconds per epoch with `memo`
(3.4 to 5.2 s of them in `reprocess`; `getx` adds ~2 s), 2 to 2.5 s with `secant`. The
macros took 9.7 to 13.1 s per epoch once compiled, and 144 s for 2016GH from a clean
checkout. Building the stage from scratch takes about a minute.

## Open items

From `../toDo.txt`:
* One output file per step instead of updates of one shared `jecdata<epoch>.root`; then
  `forfit` in `config/Inputs.json` points to the new files (F8).
* `reprocess` recreates its output before it has checked its inputs (F8).
* `hruncl` is written but never read; kept for byte parity (D5).
* The output of `globalFitRenormPF` is not used by the fit (F9).
