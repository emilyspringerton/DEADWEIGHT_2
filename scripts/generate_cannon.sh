#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

# Regenerates cannon/*.prn + cannon/*_gen.c from cannon/*.llll (EMILY/BACKLOG.md SECTION 549:
# "cannon programming" -- a real LO-compiled decision function wired into D2's own weapon-fire
# logic, see docs/LO_CANNON_PROGRAMMING.md for the full design). The three files per program are
# all committed (source of record + the two generated artifacts, same "commit the generated output,
# regenerate via a documented script" pattern scripts/generate_construct.sh already established for
# Principle 21) precisely so scripts/build.sh and CI never need a sibling LO/PARENA checkout at all
# -- only this script does, and only when a .llll source actually changes.
#
# Needs sibling ../LO and ../PARENA checkouts (this monorepo's own real layout: every repo is a
# sibling directory under one parent, matching the relative paths LO's own test suite already uses,
# e.g. LO/internal/emitter/emitter_test.go's "../../../PARENA/..."), with PARENA already built
# (`cd ../PARENA && make build`) and LO's compiler built (`cd ../LO && go build -o /tmp/lo-build
# ./cmd/lo`, or any `lo` binary on PATH).

LO_BIN="${LO_BIN:-../LO/lo}"
[ -x "$LO_BIN" ] || LO_BIN="$(command -v lo || true)"
PARENA_BIN="${PARENA_BIN:-../PARENA/parena}"
ALGEBRA_PRN="${ALGEBRA_PRN:-../PARENA/stdlib/base4/algebra.prn}"

if [ -z "$LO_BIN" ] || [ ! -x "$LO_BIN" ]; then
    echo "generate_cannon.sh: no lo compiler found -- build one: cd ../LO && go build -o /tmp/lo-build ./cmd/lo && LO_BIN=/tmp/lo-build $0" >&2
    exit 1
fi
if [ ! -x "$PARENA_BIN" ]; then
    echo "generate_cannon.sh: $PARENA_BIN not found/executable -- build it: cd ../PARENA && make build" >&2
    exit 1
fi
if [ ! -f "$ALGEBRA_PRN" ]; then
    echo "generate_cannon.sh: $ALGEBRA_PRN not found -- is ../PARENA a real PARENA checkout?" >&2
    exit 1
fi

for name in cannon_decision cannon_bank_on_safe_lead; do
    src="cannon/$name.llll"
    prn="cannon/$name.prn"
    genc="cannon/${name}_gen.c"
    [ -f "$src" ] || { echo "generate_cannon.sh: missing $src" >&2; exit 1; }

    "$LO_BIN" build "$src" -o "$prn"
    "$PARENA_BIN" build "$ALGEBRA_PRN" "$prn" -o "$genc"
    echo "generate_cannon.sh: $src -> $prn -> $genc"
done

echo "Regenerated cannon/*.prn + cannon/*_gen.c -- review the diff before committing (both files are"
echo "machine output, do not hand-edit; a clean diff after a no-op source change is the real proof"
echo "this script itself is deterministic, matching Principle 21's own construct-generation bar)."
