#!/bin/bash
# Parity test of the input creation: the C++ chain must reproduce the golden snapshots
# input/test/golden/<epoch>/<n>_<step>.root (the pre-fit file after each step, made by
# the ROOT macros' chain, recorded 2026-11-19; see input/test/README.md) byte for byte:
# every object, every key cycle (jecCompare --bytes --complete).
#
# The four epochs run in ONE jecMakeInputs process, from scratch (reprocess creates the
# file), so the test also catches ROOT state that leaks from one epoch to the next. The
# JEC inversion is the default of Reprocess.json (memo, exact); INVERSION=getx tests the
# reference mode. GOLDEN_INPUTS=<dir> points to the goldens elsewhere.
cd "$(dirname "$0")/../.." || exit 1  # the repository root
golden=${GOLDEN_INPUTS:-input/test/golden}
work=input/obj/test/parity
epochs="2016BCDEF 2016GH 2017BCDEF 2018ABCD"

for e in $epochs; do
  if [ ! -f "$golden/$e/5_globalFitRenormPF.root" ]; then
    echo "parity: SKIPPED, $golden/$e/ is incomplete (see input/test/README.md)"
    exit 0
  fi
done
rm -rf "$work"
mkdir -p "$work"
options=()
[ -n "$INVERSION" ] && options=(--jec-inversion "$INVERSION")
if ! input/jecMakeInputs -e "$(echo $epochs | tr ' ' ',')" --outdir "$work/out" --snapshot "$work/snapshots" \
     "${options[@]}" > "$work/log" 2>&1; then
  echo "parity: jecMakeInputs FAILED, see $work/log"; tail -5 "$work/log"; exit 1
fi
grep '^jecMakeInputs:' "$work/log" | sed 's/^/  /'
status=0
for e in $epochs; do
  echo "== parity $e"
  for reference in "$golden/$e"/*.root; do
    tag=$(basename "$reference" .root)
    if ! out=$(./jecCompare "$work/snapshots/$e/$tag.root" "$reference" --bytes --complete); then
      status=1
      echo "$out" | head -20
    fi
    printf "  %-22s%s\n" "$tag" "$(echo "$out" | tail -1)"
  done
done
[ $status -eq 0 ] && echo "parity: OK" || echo "parity: FAILED"
exit $status
