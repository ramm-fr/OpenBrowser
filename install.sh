#!/bin/bash
# OpenBrowser - One-click install script
set -e

echo "╔══════════════════════════════════════╗"
echo "║   OpenBrowser Installer v0.1         ║"
echo "╚══════════════════════════════════════╝"
echo ""

# Install dependencies
echo "[1/4] Installing dependencies..."
sudo apt install -y git meson ninja-build libgtk-4-dev libwebkitgtk-6.0-dev libadwaita-1-dev libjson-glib-dev libsoup-3.0-dev

# Clone if not already in the repo
echo "[2/4] Getting source code..."
if [ ! -f "meson.build" ]; then
    cd /tmp
    rm -rf OpenBrowser
    git clone https://github.com/ramm-fr/OpenBrowser.git
    cd OpenBrowser
fi

# Build
echo "[3/4] Building..."
rm -rf builddir
meson setup builddir --prefix=/usr
ninja -C builddir

# Install
echo "[4/4] Installing system-wide..."
sudo ninja -C builddir install

echo ""
echo "╔══════════════════════════════════════╗"
echo "║   ✓ OpenBrowser installed!           ║"
echo "║   Run: openbrowser                   ║"
echo "╚══════════════════════════════════════╝"
