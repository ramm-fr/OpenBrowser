#!/bin/bash
# Simple build and run script for development
set -e

echo "=== Building OpenBrowser ==="

if [ ! -d "builddir" ]; then
    echo "Configuring..."
    meson setup builddir --prefix=/usr
fi

echo "Compiling..."
ninja -C builddir

echo ""
echo "=== Build successful! ==="
echo "Run with: ./builddir/openbrowser"
