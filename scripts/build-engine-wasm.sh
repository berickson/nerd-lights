#!/usr/bin/env bash
# Build the pattern engine (lib/nl_engine) to WebAssembly, for node tests and
# the web preview. Output: build/wasm/nl_engine.mjs and nl_engine.wasm.
#
#   scripts/build-engine-wasm.sh
#
# Needs emsdk 6.0.12 at ~/emsdk (or EMSDK set). The flags that matter for
# matching the ESP32 are -ffp-contract=off and no fast-math, as in platformio.ini.

set -euo pipefail

FW_REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=${FW_REPO}/build/wasm
EMSDK=${EMSDK:-$HOME/emsdk}

# shellcheck disable=SC1091
source "${EMSDK}/emsdk_env.sh" > /dev/null 2>&1
mkdir -p "$OUT"

emcc -O2 -std=c++17 -ffp-contract=off -fno-fast-math -fno-exceptions -fno-rtti \
  "${FW_REPO}"/lib/nl_engine/src/*.cpp \
  -o "${OUT}/nl_engine.mjs" \
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node \
  -sALLOW_MEMORY_GROWTH=1 \
  -sEXPORTED_FUNCTIONS=_nl_selftest,_nl_program_create,_nl_program_free,_nl_program_render,_nl_program_bytes,_nl_config_defaults,_nl_effect_defaults,_nl_base_name,_nl_base_flags,_nl_base_param_count,_nl_base_param,_nl_effect_name,_nl_effect_param_count,_nl_effect_param,_nl_config_size,_nl_config_set_param,_nl_config_set_colors,_nl_config_set_color,_nl_config_set_brightness,_nl_config_set_seed,_nl_config_add_effect,_nl_config_set_effect_param,_malloc,_free \
  -sEXPORTED_RUNTIME_METHODS=UTF8ToString,HEAPU8,HEAPF32,HEAPU32

echo "built ${OUT}/nl_engine.mjs ($(stat -c %s "${OUT}/nl_engine.wasm") byte wasm)"
