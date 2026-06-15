#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include <adwaita.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

#define SETTINGS_TYPE_MANAGER (settings_manager_get_type())
G_DECLARE_FINAL_TYPE(SettingsManager, settings_manager, SETTINGS, MANAGER, GObject)

/* Theme options */
typedef enum {
    THEME_LIGHT,
    THEME_DARK,
    THEME_AUTO,
    THEME_AMOLED,
} BrowserTheme;

/* Search engines */
typedef enum {
    SEARCH_GOOGLE,
    SEARCH_DUCKDUCKGO,
    SEARCH_BING,
    SEARCH_BRAVE,
    SEARCH_STARTPAGE,
    SEARCH_YAHOO,
    SEARCH_YANDEX,
} SearchEngine;

SettingsManager *settings_manager_get_default(void);

/* General */
const char *settings_manager_get_homepage(SettingsManager *self);
void settings_manager_set_homepage(SettingsManager *self, const char *url);
SearchEngine settings_manager_get_search_engine(SettingsManager *self);
void settings_manager_set_search_engine(SettingsManager *self, SearchEngine engine);
const char *settings_manager_get_search_url(SettingsManager *self);
const char *settings_manager_get_download_path(SettingsManager *self);
void settings_manager_set_download_path(SettingsManager *self, const char *path);

/* Appearance */
BrowserTheme settings_manager_get_theme(SettingsManager *self);
void settings_manager_set_theme(SettingsManager *self, BrowserTheme theme);
gboolean settings_manager_get_show_bookmarks_bar(SettingsManager *self);
void settings_manager_set_show_bookmarks_bar(SettingsManager *self, gboolean show);
gboolean settings_manager_get_compact_mode(SettingsManager *self);
void settings_manager_set_compact_mode(SettingsManager *self, gboolean compact);

/* Privacy & Security */
gboolean settings_manager_get_block_trackers(SettingsManager *self);
void settings_manager_set_block_trackers(SettingsManager *self, gboolean block);
gboolean settings_manager_get_block_ads(SettingsManager *self);
void settings_manager_set_block_ads(SettingsManager *self, gboolean block);
gboolean settings_manager_get_https_only(SettingsManager *self);
void settings_manager_set_https_only(SettingsManager *self, gboolean enabled);
gboolean settings_manager_get_block_popups(SettingsManager *self);
void settings_manager_set_block_popups(SettingsManager *self, gboolean block);
gboolean settings_manager_get_do_not_track(SettingsManager *self);
void settings_manager_set_do_not_track(SettingsManager *self, gboolean enabled);
gboolean settings_manager_get_block_third_party_cookies(SettingsManager *self);
void settings_manager_set_block_third_party_cookies(SettingsManager *self, gboolean block);

/* Performance */
gboolean settings_manager_get_hardware_acceleration(SettingsManager *self);
void settings_manager_set_hardware_acceleration(SettingsManager *self, gboolean enabled);
gboolean settings_manager_get_lazy_loading(SettingsManager *self);
void settings_manager_set_lazy_loading(SettingsManager *self, gboolean enabled);
gboolean settings_manager_get_memory_saver(SettingsManager *self);
void settings_manager_set_memory_saver(SettingsManager *self, gboolean enabled);

/* Accessibility */
gboolean settings_manager_get_reduced_motion(SettingsManager *self);
void settings_manager_set_reduced_motion(SettingsManager *self, gboolean enabled);

void settings_manager_save(SettingsManager *self);
GtkWidget *settings_manager_create_page(SettingsManager *self);

G_END_DECLS

#endif /* SETTINGS_MANAGER_H */
