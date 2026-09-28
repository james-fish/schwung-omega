#!/usr/bin/env bash
# glibc_gate.sh — on-device load gate for the cross-compiled dsp.so (FNDTN-04).
#
# Fails (nonzero) if:
#   1. any referenced GLIBC_x.y symbol version is > 2.35, OR
#   2. any libmvec vectorized-math symbol (_ZGV*) leaked in, OR
#   3. move_plugin_init_v2 is not exactly the one default-visibility export.
#
# Usage: ./scripts/glibc_gate.sh build/dsp.so
set -euo pipefail

SO="${1:?usage: glibc_gate.sh <path-to-dsp.so>}"

if [ ! -f "$SO" ]; then
    echo "glibc_gate: file not found: $SO" >&2
    exit 1
fi

# 1. GLIBC <= 2.35 gate.
if objdump -T "$SO" | grep -oE 'GLIBC_[0-9]+\.[0-9]+' \
   | sort -uV | awk -F_ '{split($2,v,"."); if (v[1]>2 || (v[1]==2 && v[2]>35)) exit 1}'; then
    echo "glibc gate OK (<=2.35)"
else
    echo "GLIBC symbol > 2.35 found" >&2
    exit 1
fi

# 2. libmvec leak check.
if objdump -T "$SO" | grep -E '_ZGV|libmvec'; then
    echo "libmvec leak" >&2
    exit 1
else
    echo "libmvec gate OK (no _ZGV/libmvec symbols)"
fi

# 3. Exactly one default-visibility export: move_plugin_init_v2.
EXPORTS=$(objdump -T "$SO" | grep ' g ' | grep -c move_plugin_init_v2 || true)
if [ "$EXPORTS" -eq 1 ]; then
    echo "export gate OK (exactly one move_plugin_init_v2)"
else
    echo "export gate FAILED (found $EXPORTS move_plugin_init_v2 exports, expected 1)" >&2
    exit 1
fi

echo "glibc_gate: ALL CHECKS PASSED for $SO"
