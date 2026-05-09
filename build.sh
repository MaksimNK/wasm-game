#!/bin/bash
set -e

# Fetch deps if missing
if [ ! -f "deps/kiss_fft.h" ] || [ ! -f "deps/minimp3.h" ]; then
    bash fetch_deps.sh
fi

if [ "$1" = "wasm" ] || [ "$1" = "emscripten" ]; then
    echo "=== Building for WebAssembly (Emscripten) ==="
    
    if ! command -v emcc &> /dev/null; then
        echo "Error: emcc not found. Please source emsdk_env.sh first."
        exit 1
    fi
    
    mkdir -p out
    
    emcc src/main.cpp src/audio.cpp src/game.cpp src/render.cpp deps/kiss_fft.c -o out/game.js \
        -I include \
        -I deps \
        -s USE_SDL=2 \
        -s WASM=1 \
        -s EXPORTED_FUNCTIONS='["_main"]' \
        -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
        -s ALLOW_MEMORY_GROWTH=1 \
        --preload-file audio \
        -lm \
        -O2
    
    echo "Build complete: out/game.js, out/game.wasm"
else
    echo "=== Building native binary with Clang ==="
    
    clang++ src/main.cpp src/audio.cpp src/game.cpp src/render.cpp deps/kiss_fft.c -o game \
        -I include \
        -I deps \
        $(sdl2-config --cflags --libs) \
        -lm \
        -std=c++11 \
        -O2
    
    echo "Build complete: game"
fi
