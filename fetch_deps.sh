#!/bin/bash
set -e

DEPS_DIR="deps"
mkdir -p "$DEPS_DIR"

echo "Fetching dependencies..."

# minimp3
if [ ! -f "$DEPS_DIR/minimp3.h" ]; then
    echo "  - minimp3"
    curl -sL -o "$DEPS_DIR/minimp3.h" https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h
    curl -sL -o "$DEPS_DIR/minimp3_ex.h" https://raw.githubusercontent.com/lieff/minimp3/master/minimp3_ex.h
fi

# kissfft
if [ ! -f "$DEPS_DIR/kiss_fft.h" ]; then
    echo "  - kissfft"
    curl -sL -o "$DEPS_DIR/kiss_fft.h" https://raw.githubusercontent.com/mborgerding/kissfft/master/kiss_fft.h
    curl -sL -o "$DEPS_DIR/kiss_fft.c" https://raw.githubusercontent.com/mborgerding/kissfft/master/kiss_fft.c
    curl -sL -o "$DEPS_DIR/_kiss_fft_guts.h" https://raw.githubusercontent.com/mborgerding/kissfft/master/_kiss_fft_guts.h
fi

# kissfft logging shim (not in upstream, but needed)
if [ ! -f "$DEPS_DIR/kiss_fft_log.h" ]; then
    cat > "$DEPS_DIR/kiss_fft_log.h" << 'EOF'
#ifndef KISS_FFT_LOG_H
#define KISS_FFT_LOG_H
#include <stdio.h>
#define KISS_FFT_ERROR(...) do { fprintf(stderr, "KISS_FFT ERROR: " __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#define KISS_FFT_WARNING(...) do { fprintf(stderr, "KISS_FFT WARNING: " __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#define KISS_FFT_INFO(...) do { fprintf(stdout, "KISS_FFT INFO: " __VA_ARGS__); fprintf(stdout, "\n"); } while(0)
#define KISS_FFT_DEBUG(...)
#endif
EOF
fi

echo "Dependencies ready."
