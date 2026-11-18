# JecSys

Minimal extract of [jecsys](../jecsys) containing only the code and inputs needed to run
the Run 2 UL L3Res global fit chain (`runMe.sh`) for 2016BCDEF, 2016GH, 2017BCDEF and 2018ABCD.

## Run

```bash
source runMe.sh
```

Only the code is tracked in git; the `.root` and `.txt` inputs are ignored and must be copied
in from jecsys (same relative paths) before running.

Each line runs `mk_reprocess_RunEpoch.C("<epoch>")` -> `mk_reprocess.C`, which compiles
(ACLiC) and runs, in order:

1. `reprocess.C`      – merge inputs into `rootfiles/jecdata<epoch>.root`
2. `softrad3.C`       – FSR / HDM corrections (|eta|<1.3 and <2.5)
3. `globalFitSyst.C`  – central systematics
4. `globalFitRenormPF.C`
5. `globalFitL3Res.C` – global fit; plots in `pdf/<epoch>/`, appends to `textFiles/globalFitL3Res.txt`

Helpers: `tools.C/h`, `Flavor.C/h`, `tdrstyle_mod15.C`, `CondFormats/JetMETObjects/` (standalone JEC classes).

## Notes

- The 2016BCDEF line in `runMe.sh` is commented out. 2016GH uses 2016BCDEF as its reference IOV
  and reads `rootfiles/jecdata2016BCDEF.root`, so that file is included as an input. Uncommenting the
  line regenerates it.
- `reprocess.C` also reads a few inputs from the sibling repository `../JERCProtoLab`
  (Z+jet combinations and UL16 multijet files), so JecSys must sit next to `JERCProtoLab`.
- `rootfiles/hadWMC16GHV7_JEC.root` is a symlink to a stand-in made from `jecdataRun2Test.root`
  (see `missing.md` in jecsys).
- ROOT may print `double free or corruption` when exiting after `globalFitL3Res`; this happens after
  all outputs are written and is inherited from jecsys.
