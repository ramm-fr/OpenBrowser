<p align="center">
  <img src="poster.svg" alt="OpenBrowser" width="100%">
</p>

# OpenBrowser

A fast, private, and open-source web browser built natively for Linux with GTK4, libadwaita, and WebKitGTK.

## Features

- Vertical tab panel (collapsible)
- Download manager with real-time progress
- Password manager with auto-save & auto-fill
- Bookmark manager
- History tracking
- 7 search engines (Google, DuckDuckGo, Bing, Yahoo, Brave, Startpage, Yandex)
- Persistent login sessions (cookies saved)
- Privacy: tracker blocking, ad blocking, HTTPS-only
- Ctrl+Scroll zoom
- Keyboard shortcuts
- Custom startup page
- GNOME-style dark gray theme
- Developer tools

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+T | New Tab |
| Ctrl+W | Close Tab |
| Ctrl+R / F5 | Reload |
| Ctrl+L | Focus URL Bar |
| Ctrl+Tab | Next Tab |
| Ctrl+Shift+Tab | Previous Tab |
| Ctrl+F | Find in Page |
| Ctrl+D | Bookmark Page |
| Ctrl+H | History |
| Ctrl+P | Print |
| Ctrl+Shift+I | Developer Tools |
| Ctrl++ / Ctrl+- | Zoom In/Out |
| Ctrl+0 | Reset Zoom |
| F11 | Fullscreen |

## Built With

- GTK4
- WebKitGTK 6.0
- libadwaita
- libsoup 3.0
- JSON-GLib
- C17
- Meson

## License

GPL-3.0-or-later

## Installation

### Flatpak (Flathub)

Flatpak packaging lives in `flatpak/`. See [flatpak/FLATHUB.md](flatpak/FLATHUB.md) for submission steps.

```bash
flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install flathub io.github.ramm_fr.OpenBrowser
```

### AppImage (Recommended)

```bash
wget https://github.com/ramm-fr/OpenBrowser/releases/download/v0.1/OpenBrowser-0.1-x86_64.AppImage
chmod +x OpenBrowser-0.1-x86_64.AppImage
./OpenBrowser-0.1-x86_64.AppImage
```

### Build from Source (Full Installation with Desktop Application)

```bash
# 1. Install build dependencies
sudo apt install -y git meson ninja-build libgtk-4-dev libwebkitgtk-6.0-dev libadwaita-1-dev libjson-glib-dev libsoup-3.0-dev

# 2. Clone the repo (or pull latest if already cloned)
git clone https://github.com/ramm-fr/OpenBrowser.git
cd OpenBrowser
git pull origin main

# 3. Configure and build
meson setup builddir --prefix=/usr --wipe
ninja -C builddir

# 4. Install system-wide (binary + desktop file + icon)
sudo ninja -C builddir install

# 5. Update desktop database so it shows in app launcher
sudo update-desktop-database /usr/share/applications
sudo gtk-update-icon-cache /usr/share/icons/hicolor
```

## Uninstallation

```bash
# If installed via ninja install
sudo ninja -C builddir uninstall

# Or manually remove
sudo rm -f /usr/bin/openbrowser
sudo rm -f /usr/share/applications/com.openbrowser.desktop
sudo rm -f /usr/share/icons/hicolor/256x256/apps/com.openbrowser.png

# Remove saved data
rm -rf ~/.local/share/openbrowser
rm -rf ~/.cache/openbrowser
rm -rf ~/.config/openbrowser
```

## Author

**ramm.frr**

- [GitHub](https://github.com/ramm-fr)
- [Instagram](https://www.instagram.com/ramm.frr/)
