#!/bin/sh
# Build de l'emulateur navigateur (WASM single-file) — necessite emscripten.
set -e
cd "$(dirname "$0")"
emcc -c ../../src/qrcodegen.c -O2 -o /tmp/qrcodegen_emu.o
emcc emu.cpp /tmp/qrcodegen_emu.o -O2 -std=c++17 -I ../../src \
  -sSINGLE_FILE=1 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web,node \
  -sEXPORTED_FUNCTIONS=_emu_init,_emu_frame,_emu_fb,_emu_set_now,_emu_mode,_emu_draw_seg,_emu_draw_clear \
  -sEXPORTED_RUNTIME_METHODS=HEAPU16 \
  -o emu.js
echo "OK -> ouvrir index.html"
