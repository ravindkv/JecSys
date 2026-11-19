# Tests of the input creation

`make -C input test` runs

1. the unit tests `input/test/test*.cpp` (no input files needed): `GraphTools`, and the
   settings of every `input/config/*.json` for every epoch; and
2. the parity test `input/test/parity.sh`: the chain, run for the four epochs in one
   `jecMakeInputs` process, must reproduce the golden snapshots byte for byte, every
   object and every key cycle (`jecCompare --bytes --complete`). `INVERSION=getx` runs
   it with the reference JEC inversion instead of the default `memo`.

## Golden snapshots

`input/test/golden/<epoch>/<n>_<step>.root` is the pre-fit file of the epoch after each
step of the chain, for 2016BCDEF, 2016GH, 2017BCDEF and 2018ABCD:

| file | after |
|---|---|
| `1_reprocess.root` | reprocess |
| `2_softrad3_13.root` | softRad, \|η\| < 1.3 |
| `3_softrad3_25.root` | softRad, \|η\| < 2.5 |
| `4_globalFitSyst.root` | globalFitSyst |
| `5_globalFitRenormPF.root` | globalFitRenormPF (the final file) |

The names are the tags of `input/config/InputChain.json`. They were recorded on
2026-11-19 with the Python port of the ROOT macros, whose output was byte-identical to
the macros' (`5_globalFitRenormPF.root` equals the file the macros wrote). Both the
macros and the Python port are gone, so these files are the only record of the old
chain's output: they are not in git (115 MB), keep a copy. `GOLDEN_INPUTS=<dir>` points
the parity test to another copy.

When the output is changed on purpose, make new goldens from the C++ chain and say so in
the commit:

```bash
input/jecMakeInputs -e all --outdir /tmp/jecdata --snapshot input/test/golden
```
