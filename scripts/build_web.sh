#!/bin/sh
# Build the Omega browser bench: chain_params + UI scripts, omega.wasm, then dist/omega.html.
#   scripts/build_web.sh          (needs python3, node, zig: brew install zig | pip install ziglang)
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
find_zig() {
  if [ -n "${ZIG:-}" ]; then echo "$ZIG"; return; fi
  if command -v zig >/dev/null 2>&1; then command -v zig; return; fi
  python3 -c 'import ziglang, os; print(os.path.join(os.path.dirname(ziglang.__file__), "zig"))' 2>/dev/null || true
}
ZIG=$(find_zig)
[ -n "$ZIG" ] && [ -x "$ZIG" ] || { echo "zig not found: brew install zig  or  python3 -m pip install ziglang" >&2; exit 1; }
python3 tools/gen_chain_params.py
python3 tools/build_ui.py
node tests/test_ui.mjs
mkdir -p build
rm -f build/omega.wasm
SRCS=$(ls src/*.c src/models/*.c | grep -v dr32_engine.c)
# shellcheck disable=SC2086
"$ZIG" cc -target wasm32-wasi -mexec-model=reactor -DOMEGA_WASM -std=gnu11 -O2 -fno-math-errno \
  -ffile-prefix-map="$ROOT"=. -Isrc -o build/omega.wasm $SRCS web/wasm_host.c -lm \
  -Wl,--no-entry -Wl,-z,stack-size=1048576 -Wl,--strip-all -Wl,--initial-memory=33554432 \
  -Wl,--max-memory=134217728 -Wl,--export-dynamic
node tests/test_wasm.mjs
python3 scripts/build_web.py
mkdir -p docs && cp dist/omega.html docs/index.html
echo "open dist/omega.html"
