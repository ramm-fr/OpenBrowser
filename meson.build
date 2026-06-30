project('openbrowser', 'c',
  version: '0.1',
  license: 'GPL-3.0-or-later',
  default_options: [
    'c_std=c17',
    'warning_level=2',
  ],
)

gnome = import('gnome')

# Dependencies
gtk4_dep = dependency('gtk4', version: '>= 4.6')
webkit_dep = dependency('webkitgtk-6.0', version: '>= 2.38')
libadwaita_dep = dependency('libadwaita-1', version: '>= 1.2')
json_glib_dep = dependency('json-glib-1.0')
soup_dep = dependency('libsoup-3.0')

# Source files
src = files(
  'src/main.c',
  'src/browser-window.c',
  'src/browser-tab.c',
  'src/download-manager.c',
  'src/bookmark-manager.c',
  'src/history-manager.c',
  'src/settings-manager.c',
  'src/password-manager.c',
  'src/extension-manager.c',
)

# Resources
resources = gnome.compile_resources(
  'openbrowser-resources',
  'data/openbrowser.gresource.xml',
  source_dir: 'data',
)

executable('openbrowser',
  src,
  resources,
  dependencies: [gtk4_dep, webkit_dep, libadwaita_dep, json_glib_dep, soup_dep],
  install: true,
  install_dir: get_option('bindir'),
)

# Install desktop file, metainfo, and icons
install_data('data/io.github.ramm_fr.OpenBrowser.desktop',
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

install_data('data/io.github.ramm_fr.OpenBrowser.metainfo.xml',
  install_dir: join_paths(get_option('datadir'), 'metainfo'),
)

install_data('data/flower_icon.png',
  install_dir: join_paths(get_option('datadir'), 'icons/hicolor/256x256/apps'),
  rename: 'io.github.ramm_fr.OpenBrowser.png',
)

install_data('data/flower_icon.png',
  install_dir: join_paths(get_option('datadir'), 'icons/hicolor/128x128/apps'),
  rename: 'io.github.ramm_fr.OpenBrowser.png',
)
