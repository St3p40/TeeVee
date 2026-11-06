#!/usr/bin/env bash
# (bash: emsdk_env.sh finds its own folder through BASH_SOURCE; under a plain sh such as dash it does not, and em++ is not found)
# Builds the browser version into dist/ (teevee.js + teevee.wasm; index.html is copied there too).
# Needs the Emscripten SDK: git clone https://github.com/emscripten-core/emsdk ~/.emsdk && ~/.emsdk/emsdk install latest && ~/.emsdk/emsdk activate latest
set -e
cd "$(dirname "$0")"
EMSDK="${EMSDK:-$HOME/.emsdk}"
. "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
mkdir -p dist
em++ -O2 -std=c++17 -Wall -Wno-unused-parameter -DTEEVEE_MIND_N=128 \
  -I../src/core -I../desktop \
  wasm_main.cpp ../desktop/engine.cpp ../desktop/platform_lcd.cpp \
  ../src/core/renderer.cpp ../src/core/render_common.cpp ../src/core/render_fx.cpp ../src/core/render_widgets.cpp ../src/core/font_data.cpp \
  -sMODULARIZE=1 -sEXPORT_NAME=createTeevee -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 \
  -sEXPORTED_RUNTIME_METHODS=cwrap,FS,HEAPU32,HEAPF32,UTF8ToString,stringToUTF8,lengthBytesUTF8 \
  -o dist/teevee.js
# the build's stamp on the engine's file names (index.html asks for teevee.js?v=STAMP and the .wasm likewise), so caches never mix builds
STAMP=$(date +%s)
sed "s/__BUILD__/$STAMP/g" index.html > dist/index.html
cp manifest.webmanifest sw.js dist/
mkdir -p dist/icons && cp icons/icon-192.png icons/icon-512.png icons/icon.svg dist/icons/
ls -l dist
