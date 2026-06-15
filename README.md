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
- .deb package support

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
- JSON-GLib
- C17
- Meson

## License

GPL-3.0-or-later

## Installation

```bash
# Download from GitHub
wget https://github.com/ramm-fr/OpenBrowser/raw/main/openbrowser_1.0.0-1_amd64.deb

# Install
sudo dpkg -i openbrowser_1.0.0-1_amd64.deb

# Fix dependencies if needed
sudo apt -f install
```

## Uninstallation

```bash
# Remove the application (includes debug symbols)
sudo dpkg --purge openbrowser-dbgsym openbrowser

# Remove saved data (bookmarks, history, passwords, cookies)
rm -rf ~/.local/share/openbrowser
rm -rf ~/.cache/openbrowser
rm -rf ~/.config/openbrowser

# Remove the .deb file if downloaded
rm -f openbrowser_1.0.0-1_amd64.deb
```

## Author

**ramm.frr**

- [GitHub](https://github.com/ramm-fr)
- [Instagram](https://www.instagram.com/ramm.frr/)
