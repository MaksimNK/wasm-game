#!/bin/bash

set -e

# Detect if we're building for Emscripten
if [ "$1" = "wasm" ] || [ "$1" = "emscripten" ]; then
    echo "=== Building for WebAssembly (Emscripten) ==="
    
    if ! command -v emcc &> /dev/null; then
        echo "Error: emcc not found. Please source emsdk_env.sh first."
        exit 1
    fi
    
    emcc src/main.cpp -o game.js \
        -s USE_SDL=2 \
        -s WASM=1 \
        -s EXPORTED_FUNCTIONS='["_main"]' \
        -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
        -s ALLOW_MEMORY_GROWTH=1 \
        -O2
    
    echo "Build complete: game.js, game.wasm"
else
    echo "=== Building native binary with Clang ==="
    
    clang++ src/main.cpp -o game \
        $(sdl2-config --cflags --libs) \
        -std=c++11 \
        -O2
    
    echo "Build complete: game"
fi
