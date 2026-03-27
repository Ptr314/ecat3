#!/bin/bash
# Build script for eCat3 WASM version
# Prerequisites: Emscripten SDK installed and activated (source emsdk_env.sh)
#
# Usage: ./build_wasm.sh [clean]
#
# Output goes to deploy-wasm/ in the project root.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SRC_DIR="$PROJECT_ROOT/src"
DEPLOY_DIR="$PROJECT_ROOT/deploy"
BUILD_DIR="$PROJECT_ROOT/build-wasm"
OUTPUT_DIR="$PROJECT_ROOT/deploy-wasm"

# Check for Emscripten
if ! command -v emcmake &> /dev/null; then
    echo "Error: emcmake not found. Please install and activate the Emscripten SDK:"
    echo "  git clone https://github.com/emscripten-core/emsdk.git"
    echo "  cd emsdk && ./emsdk install latest && ./emsdk activate latest"
    echo "  source ./emsdk_env.sh"
    exit 1
fi

# Clean build if requested
if [ "$1" = "clean" ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    rm -rf "$OUTPUT_DIR"
fi

# Step 1: Build WASM binary
echo "=== Building WASM binary ==="
emcmake cmake -B "$BUILD_DIR" -S "$SRC_DIR/wasm" \
    -DCMAKE_BUILD_TYPE=Release

cmake --build "$BUILD_DIR" --parallel

# Step 2: Package machine assets
echo ""
echo "=== Packaging machine assets ==="
mkdir -p "$OUTPUT_DIR"
python3 "$SRC_DIR/wasm/package_machines.py" "$DEPLOY_DIR" "$OUTPUT_DIR"

# Step 3: Copy WASM output files
echo ""
echo "=== Copying output files ==="
cp "$BUILD_DIR/ecat3.js" "$OUTPUT_DIR/"
cp "$BUILD_DIR/ecat3.wasm" "$OUTPUT_DIR/"
cp "$BUILD_DIR/ecat3.worker.js" "$OUTPUT_DIR/" 2>/dev/null || true
cp "$SRC_DIR/wasm/ecat_wasm.js" "$OUTPUT_DIR/"

# Copy the generated HTML (from shell template)
if [ -f "$BUILD_DIR/ecat3.html" ]; then
    cp "$BUILD_DIR/ecat3.html" "$OUTPUT_DIR/index.html"
else
    # If Emscripten didn't process the shell, copy and patch manually
    cp "$SRC_DIR/wasm/shell.html" "$OUTPUT_DIR/index.html"
    # Replace {{{ SCRIPT }}} placeholder with empty string (JS is in separate file)
    sed -i 's/{{{ SCRIPT }}}//' "$OUTPUT_DIR/index.html"
fi

echo ""
echo "=== Build complete ==="
echo "Output directory: $OUTPUT_DIR"
echo ""
echo "To test locally, serve with proper headers:"
echo "  npx serve --cors -l 8080 $OUTPUT_DIR"
echo ""
echo "Or use Python with COOP/COEP headers:"
echo "  python3 $SRC_DIR/wasm/serve_wasm.py $OUTPUT_DIR"
echo ""
echo "NOTE: SharedArrayBuffer requires these HTTP headers:"
echo "  Cross-Origin-Opener-Policy: same-origin"
echo "  Cross-Origin-Embedder-Policy: require-corp"
