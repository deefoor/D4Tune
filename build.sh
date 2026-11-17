#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="${D4TUNE_ROOT:-$(cd "$HERE/.." && pwd)}"
SOURCE="$HERE/src/runtime/d4tune.c"
OUTPUT="$HERE/d4tune.bin"

fail(){ printf 'D4Tune: %s\n' "$*" >&2; exit 1; }

[[ -f "$ROOT/runtime/cpu.h" && -f "$ROOT/runtime/guest.h" && -f "$ROOT/runtime/web/compat/wasm_plat.h" ]] ||
    fail "put this D4Tune folder directly inside an nfsu2-web source checkout"
[[ -f "$SOURCE" ]] || fail "missing $SOURCE"

if [[ -z "${WASI:-}" ]]; then
    for sdk in "$ROOT"/deps/wasi-sdk-*; do
        if [[ -x "$sdk/bin/clang" ]]; then WASI="$sdk"; fi
    done
fi
[[ -n "${WASI:-}" && -x "$WASI/bin/clang" ]] ||
    fail "wasi-sdk is missing; unpack wasi-sdk 34 under deps/ or set WASI to its directory"

TARGET="--target=wasm32-wasip1-threads -pthread"
FEAT="-msimd128 -mbulk-memory -mnontrapping-fptoint -mtail-call -mexception-handling -mllvm -wasm-enable-sjlj -mllvm -wasm-use-legacy-eh=false"
DEFS="-D_WASI_EMULATED_MMAN -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS"
TMP="$OUTPUT.tmp"
trap 'rm -f "$TMP"' EXIT

"$WASI/bin/clang" $TARGET -O3 $FEAT $DEFS \
    -fno-delete-null-pointer-checks \
    -include "$ROOT/runtime/web/compat/wasm_plat.h" \
    -I"$ROOT/runtime/web/compat" -I"$ROOT/runtime" \
    -c -o "$TMP" "$SOURCE"
mv "$TMP" "$OUTPUT"
trap - EXIT

printf 'D4Tune: built relocatable WebAssembly object %s\n' "$OUTPUT"
printf 'D4Tune: link this object into the target project; it is not a live-process plugin\n'
sha256sum "$OUTPUT"
