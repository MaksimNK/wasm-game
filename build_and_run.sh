#!/bin/bash

# Build and run script for Emscripten WebAssembly

set -e

echo "=== Cleaning old files ==="
rm -f game game.js game.wasm game.html
echo "Old files deleted"

echo ""
echo "=== Building SDL2 game ==="
bash build.sh wasm

echo ""
echo "=== Checking for any process on port 6931 ==="
OLD_PID=$(lsof -ti:6931 2>/dev/null || true)
if [ -n "$OLD_PID" ]; then
    echo "Killing old server (PID: $OLD_PID)"
    kill -9 $OLD_PID 2>/dev/null || true
    sleep 1
fi

echo ""
echo "=== Starting server on port 6931 ==="
# Start server in background and suppress logs
python3 -m http.server 6931 > /dev/null 2>&1 &
SERVER_PID=$!
sleep 2

echo ""
echo "Server started with PID: $SERVER_PID"
echo ""

# Open browser
if command -v open &> /dev/null; then
    open http://localhost:6931/index.html
elif command -v xdg-open &> /dev/null; then
    xdg-open http://localhost:6931/index.html
fi

echo "========================================="
echo "Game running at: http://localhost:6931/index.html"
echo ""
echo "The server is running in the background."
echo "To stop the server, run: kill $SERVER_PID"
echo "========================================="
