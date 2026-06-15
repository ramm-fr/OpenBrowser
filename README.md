# OpenBrowser

A GNOME-style web browser built with GTK4, libadwaita, and WebKitGTK. Features a modern gray-toned interface with comprehensive browsing features.

## Features

### Browser Core
- Multi-tab browsing (pinned, private, muted, duplicate)
- Tab bar with close buttons and indicators
- Private/Incognito browsing mode
- Session management

### Navigation
- Back/Forward/Reload/Home
- Smart URL bar (auto-detects URLs vs search queries)
- Search via DuckDuckGo (configurable)
- Loading progress indicator

### Bookmarks
- Add/Remove bookmarks
- Folder organization
- Bookmark bar
- Import/Export HTML bookmarks
- Search bookmarks

### History
- Browsing history with timestamps
- Search history
- Clear history (all or by range)
- Most visited sites

### Downloads
- Download manager with progress
- Pause/Resume/Cancel
- Open file/folder
- Clear completed

### Privacy & Security
- Built-in tracker blocking
- Ad blocking
- HTTPS-only mode
- Popup blocker
- Do Not Track
- Third-party cookie blocking
- Private browsing mode

### Developer Tools
- WebKit Inspector (Ctrl+Shift+I)
- Console, Elements, Network, etc.

### Settings
- Homepage configuration
- Search engine selection
- Theme (Dark by default)
- Performance options (Hardware acceleration, lazy loading, memory saver)
- Privacy controls

### Keyboard Shortcuts
| Shortcut | Action |
|----------|--------|
| Ctrl+T | New Tab |
| Ctrl+W | Close Tab |
| Ctrl+L | Focus URL Bar |
| Ctrl+F | Find in Page |
| Ctrl+D | Bookmark Page |
| Ctrl+H | History |
| Ctrl+P | Print |
| Ctrl+Shift+I | Developer Tools |
| Ctrl+Plus | Zoom In |
| Ctrl+Minus | Zoom Out |
| Ctrl+0 | Reset Zoom |
| F11 | Fullscreen |

## Build Dependencies

```bash
sudo apt install meson ninja-build libgtk-4-dev libwebkitgtk-6.0-dev libadwaita-1-dev libjson-glib-dev
```

## Building

### Quick build (development):
```bash
./build.sh
./builddir/openbrowser
```

### Build .deb package:
```bash
sudo apt install debhelper devscripts
./build-deb.sh
```

### Manual build:
```bash
meson setup builddir --prefix=/usr
ninja -C builddir
sudo ninja -C builddir install
```

## Install .deb

```bash
sudo dpkg -i openbrowser_1.0.0-1_amd64.deb
sudo apt -f install  # fix dependencies if needed
```

## Architecture

```
src/
  main.c              - Application entry point, CSS loading, dark theme
  browser-window.c    - Main window with tabs, toolbar, sidebar, shortcuts
  browser-tab.c       - Individual tab with WebView, find-in-page
  download-manager.c  - Download tracking and management
  bookmark-manager.c  - Bookmark CRUD with JSON persistence
  history-manager.c   - History tracking with JSON persistence
  settings-manager.c  - App settings with JSON persistence
data/
  style.css           - GNOME-style gray theme
  com.openbrowser.desktop - Desktop entry
  com.openbrowser.svg - App icon
debian/
  control, rules, changelog... - Debian packaging
```

## License

GPL-3.0-or-later
