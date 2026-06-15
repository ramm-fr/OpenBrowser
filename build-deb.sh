#!/bin/bash
# Build script for OpenBrowser .deb package
set -e

echo "=== OpenBrowser .deb Build Script ==="
echo ""

# Check dependencies
echo "[1/5] Checking build dependencies..."
DEPS="meson ninja-build libgtk-4-dev libwebkitgtk-6.0-dev libadwaita-1-dev libjson-glib-dev debhelper devscripts"
MISSING=""

for dep in $DEPS; do
    if ! dpkg -l "$dep" &>/dev/null; then
        MISSING="$MISSING $dep"
    fi
done

if [ -n "$MISSING" ]; then
    echo "Missing packages:$MISSING"
    echo ""
    echo "Install them with:"
    echo "  sudo apt install$MISSING"
    echo ""
    read -p "Install now? [y/N] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        sudo apt install -y $MISSING
    else
        echo "Cannot continue without dependencies."
        exit 1
    fi
fi

echo "[2/5] Cleaning previous builds..."
rm -rf builddir
rm -f ../openbrowser_*.deb

echo "[3/5] Configuring with Meson..."
meson setup builddir --prefix=/usr --buildtype=release

echo "[4/5] Building..."
ninja -C builddir

echo "[5/5] Building .deb package..."
dpkg-buildpackage -us -uc -b

echo ""
echo "=== Build Complete ==="
echo "The .deb package is at: ../openbrowser_1.0.0-1_$(dpkg-architecture -qDEB_BUILD_ARCH).deb"
echo ""
echo "Install with:"
echo "  sudo dpkg -i ../openbrowser_1.0.0-1_$(dpkg-architecture -qDEB_BUILD_ARCH).deb"
echo "  sudo apt -f install  # Fix dependencies if needed"
