#!/bin/bash
# Parity test: the new chain (jecMakeCard -> jecWorkspace -> jecFit) must reproduce the
# fit results of the old macro globalFitL3Res.C (now removed) that are stored in the
# golden files test/golden/jecdata<epoch>.root. See test/README.md.
#
# Compared (relative tolerance 1e-9; today the agreement is exact): covariance matrix,
# parameters and errors, hjesfit with its eigenvectors and components, eigvec/eigmat,
# and the refitted FSR histograms (reference IOVs only, as the old code stored them).
cd "$(dirname "$0")/.." || exit 1
epochs="2016BCDEF 2016GH 2017BCDEF 2018ABCD"
for e in $epochs; do
  if [ ! -f test/golden/jecdata$e.root ]; then
    echo "parity: SKIPPED, test/golden/jecdata$e.root is missing (see test/README.md)"
    exit 0
  fi
done
./runFit -e all --inputs test/Inputs.golden.json > obj/test/parity.log 2>&1 || {
  echo "parity: runFit FAILED, see obj/test/parity.log"; tail -5 obj/test/parity.log; exit 1; }
status=0
for e in $epochs; do
  echo "== parity $e"
  ./jecCompare obj/test/postfit/L1L2L3Res/$e/fit.root ratio/eta00-13 \
               test/golden/jecdata$e.root ratio/eta00-13 \
               --tolerance 1e-9 --allowMissing 'fsr/hkfsr2_.*' || status=1
done
[ $status -eq 0 ] && echo "parity: OK" || echo "parity: FAILED"
exit $status
