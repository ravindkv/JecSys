# Tests

`make test` runs

1. the unit tests `test/test*.cpp` (no input files needed), and
2. the parity test `test/parity.sh`: the new chain must reproduce the fit of the old
   macro chain for the four epochs.

## Golden files for the parity test

The parity test needs `test/golden/jecdata<epoch>.root` for 2016BCDEF, 2016GH,
2017BCDEF and 2018ABCD. They are not in git (`*.root` is ignored). If they are missing
the parity test is skipped with a message.

A golden file is a `jecdata<epoch>.root` that was **fitted by the old macro**
`globalFitL3Res.C`: the test uses its pre-fit graphs as input and compares the new
fit with the fit results stored next to them (`ratio/eta00-13/sys/*`, `fsr/hkfsr2_*`).

`globalFitL3Res.C` has been removed from this package, so the golden files cannot be
remade here. They are a frozen snapshot made on 2026-11-18. Keep a copy. If they are
lost, they can be made again with the old macro from the git history of this package
(the commit before the clean-up) or from `../jecsys`:

```bash
# in a checkout that still has globalFitL3Res.C
source runMe.sh      # old layout: writes rootfiles/jecdata<epoch>.root
cp rootfiles/jecdata{2016BCDEF,2016GH,2017BCDEF,2018ABCD}.root <this package>/test/golden/
```

When the numbers are changed on purpose (a `toDo.txt` item), the old macro is no
longer the reference. Then make new golden files from the new chain itself and say so
in the commit: copy `input/forfit/reprocess/jecdata<epoch>.root` to `test/golden/` and add the
objects of `output/postfit/L1L2L3Res/<epoch>/fit.root` under `ratio/eta00-13/` to it
(a small tool for this is not written yet).
