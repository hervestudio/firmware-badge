#!/bin/sh
# Build de l'emulateur navigateur (WASM single-file) — necessite emscripten.
set -e
cd "$(dirname "$0")"
emcc -c ../../src/qrcodegen.c -O2 -o /tmp/qrcodegen_emu.o
emcc emu.cpp /tmp/qrcodegen_emu.o -O2 -std=c++17 -I ../../src \
  -sSINGLE_FILE=1 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web,node \
  -sEXPORTED_FUNCTIONS=_emu_init,_emu_frame,_emu_fb,_emu_set_now,_emu_mode,_emu_draw_seg,_emu_draw_clear,_emu_setup_json,_emu_setup_conn,_emu_setup_name,_emu_setup_company,_emu_setup_msg,_emu_setup_url,_emu_setup_buddy,_emu_setup_step,_emu_setup_building,_emu_social_seen,_free \
  -sEXPORTED_RUNTIME_METHODS=HEAPU16,UTF8ToString,stringToNewUTF8 \
  -o emu.js
echo "OK -> ouvrir index.html"
