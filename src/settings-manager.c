#include "settings-manager.h"
#include <glib/gstdio.h>

struct _SettingsManager {
    GObject parent_instance;

    char *config_file;

    /* General */
    char *homepage;
    SearchEngine search_engine;
    char *download_path;

    /* Appearance */
    BrowserTheme theme;
    gboolean show_bookmarks_bar;
    gboolean compact_mode;

    /* Privacy */
    gboolean block_trackers;
    gboolean block_ads;
    gboolean https_only;
    gboolean block_popups;
    gboolean do_not_track;
    gboolean block_third_party_cookies;

    /* Performance */
    gboolean hardware_acceleration;
    gboolean lazy_loading;
    gboolean memory_saver;

    /* Accessibility */
    gboolean reduced_motion;
};

G_DEFINE_TYPE(SettingsManager, settings_manager, G_TYPE_OBJECT)

static SettingsManager *default_instance = NULL;

static void
settings_manager_load(SettingsManager *self)
{
    if (!g_file_test(self->config_file, G_FILE_TEST_EXISTS))
        return;

    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    if (!json_parser_load_from_file(parser, self->config_file, &error)) {
        g_warning("Failed to load settings: %s", error->message);
        g_error_free(error);
        g_object_unref(parser);
        return;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonObject *obj = json_node_get_object(root);

    if (json_object_has_member(obj, "homepage")) {
        g_free(self->homepage);
        self->homepage = g_strdup(json_object_get_string_member(obj, "homepage"));
    }
    if (json_object_has_member(obj, "search_engine"))
        self->search_engine = json_object_get_int_member(obj, "search_engine");
    if (json_object_has_member(obj, "download_path")) {
        g_free(self->download_path);
        self->download_path = g_strdup(json_object_get_string_member(obj, "download_path"));
    }
    if (json_object_has_member(obj, "theme"))
        self->theme = json_object_get_int_member(obj, "theme");
    if (json_object_has_member(obj, "show_bookmarks_bar"))
        self->show_bookmarks_bar = json_object_get_boolean_member(obj, "show_bookmarks_bar");
    if (json_object_has_member(obj, "compact_mode"))
        self->compact_mode = json_object_get_boolean_member(obj, "compact_mode");
    if (json_object_has_member(obj, "block_trackers"))
        self->block_trackers = json_object_get_boolean_member(obj, "block_trackers");
    if (json_object_has_member(obj, "block_ads"))
        self->block_ads = json_object_get_boolean_member(obj, "block_ads");
    if (json_object_has_member(obj, "https_only"))
        self->https_only = json_object_get_boolean_member(obj, "https_only");
    if (json_object_has_member(obj, "block_popups"))
        self->block_popups = json_object_get_boolean_member(obj, "block_popups");
    if (json_object_has_member(obj, "do_not_track"))
        self->do_not_track = json_object_get_boolean_member(obj, "do_not_track");
    if (json_object_has_member(obj, "block_third_party_cookies"))
        self->block_third_party_cookies = json_object_get_boolean_member(obj, "block_third_party_cookies");
    if (json_object_has_member(obj, "hardware_acceleration"))
        self->hardware_acceleration = json_object_get_boolean_member(obj, "hardware_acceleration");
    if (json_object_has_member(obj, "lazy_loading"))
        self->lazy_loading = json_object_get_boolean_member(obj, "lazy_loading");
    if (json_object_has_member(obj, "memory_saver"))
        self->memory_saver = json_object_get_boolean_member(obj, "memory_saver");
    if (json_object_has_member(obj, "reduced_motion"))
        self->reduced_motion = json_object_get_boolean_member(obj, "reduced_motion");

    g_object_unref(parser);
}

static void
settings_manager_dispose(GObject *object)
{
    SettingsManager *self = SETTINGS_MANAGER(object);
    settings_manager_save(self);
    g_free(self->homepage);
    g_free(self->download_path);
    g_free(self->config_file);
    G_OBJECT_CLASS(settings_manager_parent_class)->dispose(object);
}

static void
settings_manager_class_init(SettingsManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = settings_manager_dispose;
}

static void
settings_manager_init(SettingsManager *self)
{
    self->config_file = g_build_filename(g_get_user_config_dir(), "openbrowser", "settings.json", NULL);

    /* Defaults */
    self->homepage = g_strdup("https://duckduckgo.com");
    self->search_engine = SEARCH_DUCKDUCKGO;
    self->download_path = g_strdup(g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD));
    self->theme = THEME_DARK;
    self->show_bookmarks_bar = TRUE;
    self->compact_mode = FALSE;
    self->block_trackers = TRUE;
    self->block_ads = TRUE;
    self->https_only = TRUE;
    self->block_popups = TRUE;
    self->do_not_track = TRUE;
    self->block_third_party_cookies = TRUE;
    self->hardware_acceleration = TRUE;
    self->lazy_loading = TRUE;
    self->memory_saver = TRUE;
    self->reduced_motion = FALSE;

    settings_manager_load(self);
}

SettingsManager *
settings_manager_get_default(void)
{
    if (!default_instance) {
        default_instance = g_object_new(SETTINGS_TYPE_MANAGER, NULL);
    }
    return default_instance;
}

const char *settings_manager_get_homepage(SettingsManager *self) { return self->homepage; }
void settings_manager_set_homepage(SettingsManager *self, const char *url) {
    g_free(self->homepage); self->homepage = g_strdup(url); settings_manager_save(self);
}

SearchEngine settings_manager_get_search_engine(SettingsManager *self) { return self->search_engine; }
void settings_manager_set_search_engine(SettingsManager *self, SearchEngine engine) {
    self->search_engine = engine; settings_manager_save(self);
}

const char *settings_manager_get_search_url(SettingsManager *self) {
    switch (self->search_engine) {
        case SEARCH_DUCKDUCKGO: return "https://duckduckgo.com/?q=%s";
        case SEARCH_BRAVE:      return "https://search.brave.com/search?q=%s";
        case SEARCH_STARTPAGE:  return "https://www.startpage.com/do/search?q=%s";
        case SEARCH_YAHOO:      return "https://search.yahoo.com/search?p=%s";
        case SEARCH_YANDEX:     return "https://yandex.com/search/?text=%s";
        default:                return "https://duckduckgo.com/?q=%s";
    }
}

const char *settings_manager_get_download_path(SettingsManager *self) { return self->download_path; }
void settings_manager_set_download_path(SettingsManager *self, const char *path) {
    g_free(self->download_path); self->download_path = g_strdup(path); settings_manager_save(self);
}

BrowserTheme settings_manager_get_theme(SettingsManager *self) { return self->theme; }
void settings_manager_set_theme(SettingsManager *self, BrowserTheme theme) {
    self->theme = theme; settings_manager_save(self);
}

gboolean settings_manager_get_show_bookmarks_bar(SettingsManager *self) { return self->show_bookmarks_bar; }
void settings_manager_set_show_bookmarks_bar(SettingsManager *self, gboolean show) {
    self->show_bookmarks_bar = show; settings_manager_save(self);
}

gboolean settings_manager_get_compact_mode(SettingsManager *self) { return self->compact_mode; }
void settings_manager_set_compact_mode(SettingsManager *self, gboolean compact) {
    self->compact_mode = compact; settings_manager_save(self);
}

gboolean settings_manager_get_block_trackers(SettingsManager *self) { return self->block_trackers; }
void settings_manager_set_block_trackers(SettingsManager *self, gboolean block) {
    self->block_trackers = block; settings_manager_save(self);
}

gboolean settings_manager_get_block_ads(SettingsManager *self) { return self->block_ads; }
void settings_manager_set_block_ads(SettingsManager *self, gboolean block) {
    self->block_ads = block; settings_manager_save(self);
}

gboolean settings_manager_get_https_only(SettingsManager *self) { return self->https_only; }
void settings_manager_set_https_only(SettingsManager *self, gboolean enabled) {
    self->https_only = enabled; settings_manager_save(self);
}

gboolean settings_manager_get_block_popups(SettingsManager *self) { return self->block_popups; }
void settings_manager_set_block_popups(SettingsManager *self, gboolean block) {
    self->block_popups = block; settings_manager_save(self);
}

gboolean settings_manager_get_do_not_track(SettingsManager *self) { return self->do_not_track; }
void settings_manager_set_do_not_track(SettingsManager *self, gboolean enabled) {
    self->do_not_track = enabled; settings_manager_save(self);
}

gboolean settings_manager_get_block_third_party_cookies(SettingsManager *self) { return self->block_third_party_cookies; }
void settings_manager_set_block_third_party_cookies(SettingsManager *self, gboolean block) {
    self->block_third_party_cookies = block; settings_manager_save(self);
}

gboolean settings_manager_get_hardware_acceleration(SettingsManager *self) { return self->hardware_acceleration; }
void settings_manager_set_hardware_acceleration(SettingsManager *self, gboolean enabled) {
    self->hardware_acceleration = enabled; settings_manager_save(self);
}

gboolean settings_manager_get_lazy_loading(SettingsManager *self) { return self->lazy_loading; }
void settings_manager_set_lazy_loading(SettingsManager *self, gboolean enabled) {
    self->lazy_loading = enabled; settings_manager_save(self);
}

gboolean settings_manager_get_memory_saver(SettingsManager *self) { return self->memory_saver; }
void settings_manager_set_memory_saver(SettingsManager *self, gboolean enabled) {
    self->memory_saver = enabled; settings_manager_save(self);
}

gboolean settings_manager_get_reduced_motion(SettingsManager *self) { return self->reduced_motion; }
void settings_manager_set_reduced_motion(SettingsManager *self, gboolean enabled) {
    self->reduced_motion = enabled; settings_manager_save(self);
}

gboolean
settings_manager_is_default_browser(void)
{
    g_autofree char *stdout_str = NULL;
    g_autoptr(GError) error = NULL;

    gboolean ret = g_spawn_command_line_sync(
        "xdg-settings get default-web-browser",
        &stdout_str, NULL, NULL, &error);

    if (!ret || !stdout_str)
        return FALSE;

    g_strstrip(stdout_str);
    return g_strcmp0(stdout_str, "io.github.ramm_fr.OpenBrowser.desktop") == 0;
}

void
settings_manager_set_as_default_browser(void)
{
    g_autoptr(GError) error = NULL;

    gboolean ret = g_spawn_command_line_sync(
        "xdg-settings set default-web-browser io.github.ramm_fr.OpenBrowser.desktop",
        NULL, NULL, NULL, &error);

    if (!ret) {
        g_warning("Failed to set default browser: %s",
                  error ? error->message : "unknown error");
    }
}

void
settings_manager_save(SettingsManager *self)
{
    g_return_if_fail(SETTINGS_IS_MANAGER(self));

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);

    json_builder_set_member_name(builder, "homepage");
    json_builder_add_string_value(builder, self->homepage);
    json_builder_set_member_name(builder, "search_engine");
    json_builder_add_int_value(builder, self->search_engine);
    json_builder_set_member_name(builder, "download_path");
    json_builder_add_string_value(builder, self->download_path);
    json_builder_set_member_name(builder, "theme");
    json_builder_add_int_value(builder, self->theme);
    json_builder_set_member_name(builder, "show_bookmarks_bar");
    json_builder_add_boolean_value(builder, self->show_bookmarks_bar);
    json_builder_set_member_name(builder, "compact_mode");
    json_builder_add_boolean_value(builder, self->compact_mode);
    json_builder_set_member_name(builder, "block_trackers");
    json_builder_add_boolean_value(builder, self->block_trackers);
    json_builder_set_member_name(builder, "block_ads");
    json_builder_add_boolean_value(builder, self->block_ads);
    json_builder_set_member_name(builder, "https_only");
    json_builder_add_boolean_value(builder, self->https_only);
    json_builder_set_member_name(builder, "block_popups");
    json_builder_add_boolean_value(builder, self->block_popups);
    json_builder_set_member_name(builder, "do_not_track");
    json_builder_add_boolean_value(builder, self->do_not_track);
    json_builder_set_member_name(builder, "block_third_party_cookies");
    json_builder_add_boolean_value(builder, self->block_third_party_cookies);
    json_builder_set_member_name(builder, "hardware_acceleration");
    json_builder_add_boolean_value(builder, self->hardware_acceleration);
    json_builder_set_member_name(builder, "lazy_loading");
    json_builder_add_boolean_value(builder, self->lazy_loading);
    json_builder_set_member_name(builder, "memory_saver");
    json_builder_add_boolean_value(builder, self->memory_saver);
    json_builder_set_member_name(builder, "reduced_motion");
    json_builder_add_boolean_value(builder, self->reduced_motion);

    json_builder_end_object(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_pretty(gen, TRUE);
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);

    char *dir = g_path_get_dirname(self->config_file);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    GError *error = NULL;
    if (!json_generator_to_file(gen, self->config_file, &error)) {
        g_warning("Failed to save settings: %s", error->message);
        g_error_free(error);
    }

    json_node_unref(root);
    g_object_unref(gen);
    g_object_unref(builder);
}

static void
on_search_engine_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    SettingsManager *self = SETTINGS_MANAGER(user_data);
    guint selected = gtk_drop_down_get_selected(dropdown);
    self->search_engine = (SearchEngine)selected;
    settings_manager_save(self);
}

static void
on_set_default_browser_clicked(GtkButton *button, gpointer user_data)
{
    settings_manager_set_as_default_browser();

    /* Update UI to reflect new state */
    if (settings_manager_is_default_browser()) {
        gtk_button_set_label(button, "Default ✓");
        gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
        gtk_widget_remove_css_class(GTK_WIDGET(button), "suggested-action");

        AdwActionRow *row = ADW_ACTION_ROW(user_data);
        adw_action_row_set_subtitle(row, "OpenBrowser is your default browser");
    }
}

static void
on_clear_cookies_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    char *cookie_file = g_build_filename(g_get_user_data_dir(), "openbrowser", "cookies.sqlite", NULL);
    g_remove(cookie_file);
    g_free(cookie_file);
    gtk_button_set_label(button, "Cleared ✓");
    gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
    gtk_widget_remove_css_class(GTK_WIDGET(button), "destructive-action");
}

static void
on_clear_cache_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    char *cache_dir = g_build_filename(g_get_user_cache_dir(), "openbrowser", NULL);
    /* Remove cache directory contents */
    GDir *dir = g_dir_open(cache_dir, 0, NULL);
    if (dir) {
        const char *name;
        while ((name = g_dir_read_name(dir)) != NULL) {
            char *path = g_build_filename(cache_dir, name, NULL);
            g_remove(path);
            g_free(path);
        }
        g_dir_close(dir);
    }
    g_free(cache_dir);
    gtk_button_set_label(button, "Cleared ✓");
    gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
    gtk_widget_remove_css_class(GTK_WIDGET(button), "destructive-action");
}

static void
on_clear_history_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    char *history_file = g_build_filename(g_get_user_data_dir(), "openbrowser", "history.json", NULL);
    g_remove(history_file);
    g_free(history_file);
    gtk_button_set_label(button, "Cleared ✓");
    gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
    gtk_widget_remove_css_class(GTK_WIDGET(button), "destructive-action");
}

static void
on_clear_all_data_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;

    /* Clear cookies */
    char *cookie_file = g_build_filename(g_get_user_data_dir(), "openbrowser", "cookies.sqlite", NULL);
    g_remove(cookie_file);
    g_free(cookie_file);

    /* Clear cache */
    char *cache_dir = g_build_filename(g_get_user_cache_dir(), "openbrowser", NULL);
    GDir *dir = g_dir_open(cache_dir, 0, NULL);
    if (dir) {
        const char *name;
        while ((name = g_dir_read_name(dir)) != NULL) {
            char *path = g_build_filename(cache_dir, name, NULL);
            g_remove(path);
            g_free(path);
        }
        g_dir_close(dir);
    }
    g_free(cache_dir);

    /* Clear history */
    char *history_file = g_build_filename(g_get_user_data_dir(), "openbrowser", "history.json", NULL);
    g_remove(history_file);
    g_free(history_file);

    /* Clear bookmarks */
    char *bookmarks_file = g_build_filename(g_get_user_data_dir(), "openbrowser", "bookmarks.json", NULL);
    g_remove(bookmarks_file);
    g_free(bookmarks_file);

    /* Clear passwords */
    char *passwords_file = g_build_filename(g_get_user_data_dir(), "openbrowser", "passwords.json", NULL);
    g_remove(passwords_file);
    g_free(passwords_file);

    gtk_button_set_label(button, "Cleared ✓");
    gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);
    gtk_widget_remove_css_class(GTK_WIDGET(button), "destructive-action");
}

/* Generic toggle callback - uses stored setter function pointer */
static void
on_toggle_changed(GtkSwitch *sw, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    SettingsManager *self = settings_manager_get_default();
    gboolean active = gtk_switch_get_active(sw);

    /* Get the setting key stored on the switch */
    const char *key = g_object_get_data(G_OBJECT(sw), "setting-key");
    if (!key) return;

    (void)user_data;

    if (g_strcmp0(key, "block_trackers") == 0)
        settings_manager_set_block_trackers(self, active);
    else if (g_strcmp0(key, "block_ads") == 0)
        settings_manager_set_block_ads(self, active);
    else if (g_strcmp0(key, "https_only") == 0)
        settings_manager_set_https_only(self, active);
    else if (g_strcmp0(key, "block_popups") == 0)
        settings_manager_set_block_popups(self, active);
    else if (g_strcmp0(key, "do_not_track") == 0)
        settings_manager_set_do_not_track(self, active);
    else if (g_strcmp0(key, "block_third_party_cookies") == 0)
        settings_manager_set_block_third_party_cookies(self, active);
    else if (g_strcmp0(key, "hardware_acceleration") == 0)
        settings_manager_set_hardware_acceleration(self, active);
    else if (g_strcmp0(key, "lazy_loading") == 0)
        settings_manager_set_lazy_loading(self, active);
    else if (g_strcmp0(key, "memory_saver") == 0)
        settings_manager_set_memory_saver(self, active);
}

GtkWidget *
settings_manager_create_page(SettingsManager *self)
{
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_set_hexpand(scrolled, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_NEVER, GTK_POLICY_EXTERNAL);

    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(page, 40);
    gtk_widget_set_margin_end(page, 40);
    gtk_widget_set_margin_top(page, 32);
    gtk_widget_set_margin_bottom(page, 32);
    gtk_widget_set_halign(page, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(page, TRUE);

    /* Header */
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *header_icon = gtk_image_new_from_icon_name("emblem-system-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(header_icon), 32);
    gtk_box_append(GTK_BOX(header_box), header_icon);

    GtkWidget *header_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title = gtk_label_new("Settings");
    gtk_widget_add_css_class(title, "title-1");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_append(GTK_BOX(header_text), title);

    GtkWidget *subtitle = gtk_label_new("Configure your browser");
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0);
    gtk_box_append(GTK_BOX(header_text), subtitle);

    gtk_widget_set_hexpand(header_text, TRUE);
    gtk_box_append(GTK_BOX(header_box), header_text);
    gtk_box_append(GTK_BOX(page), header_box);

    /* Separator */
    gtk_box_append(GTK_BOX(page), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* === GENERAL SECTION === */
    GtkWidget *gen_label = gtk_label_new("General");
    gtk_widget_add_css_class(gen_label, "title-4");
    gtk_label_set_xalign(GTK_LABEL(gen_label), 0);
    gtk_widget_set_margin_top(gen_label, 8);
    gtk_box_append(GTK_BOX(page), gen_label);

    GtkWidget *gen_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(gen_box), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(gen_box, "boxed-list");

    /* Homepage row */
    AdwActionRow *homepage_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(homepage_row), "Homepage");
    adw_action_row_set_subtitle(homepage_row, self->homepage);
    GtkWidget *home_icon = gtk_image_new_from_icon_name("go-home-symbolic");
    adw_action_row_add_prefix(homepage_row, home_icon);
    gtk_list_box_append(GTK_LIST_BOX(gen_box), GTK_WIDGET(homepage_row));

    /* Search Engine row */
    AdwActionRow *search_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(search_row), "Search Engine");
    adw_action_row_set_subtitle(search_row, "Choose your default search engine");
    GtkWidget *search_icon = gtk_image_new_from_icon_name("system-search-symbolic");
    adw_action_row_add_prefix(search_row, search_icon);
    GtkWidget *search_dropdown = gtk_drop_down_new_from_strings(
        (const char *[]){
            "DuckDuckGo", "Brave Search",
            "Startpage", "Yahoo", "Yandex", NULL
        });
    gtk_drop_down_set_selected(GTK_DROP_DOWN(search_dropdown), self->search_engine);
    gtk_widget_set_valign(search_dropdown, GTK_ALIGN_CENTER);
    g_signal_connect(search_dropdown, "notify::selected",
        G_CALLBACK(on_search_engine_changed), self);
    adw_action_row_add_suffix(search_row, search_dropdown);
    gtk_list_box_append(GTK_LIST_BOX(gen_box), GTK_WIDGET(search_row));

    /* Default Browser row */
    AdwActionRow *default_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(default_row), "Default Browser");
    if (settings_manager_is_default_browser()) {
        adw_action_row_set_subtitle(default_row, "OpenBrowser is your default browser");
    } else {
        adw_action_row_set_subtitle(default_row, "Set OpenBrowser as your default web browser");
    }
    GtkWidget *default_icon = gtk_image_new_from_icon_name("web-browser-symbolic");
    adw_action_row_add_prefix(default_row, default_icon);
    GtkWidget *default_btn = gtk_button_new_with_label(
        settings_manager_is_default_browser() ? "Default ✓" : "Set as Default");
    gtk_widget_set_valign(default_btn, GTK_ALIGN_CENTER);
    if (settings_manager_is_default_browser()) {
        gtk_widget_set_sensitive(default_btn, FALSE);
    } else {
        gtk_widget_add_css_class(default_btn, "suggested-action");
    }
    g_signal_connect(default_btn, "clicked", G_CALLBACK(on_set_default_browser_clicked), default_row);
    adw_action_row_add_suffix(default_row, default_btn);
    gtk_list_box_append(GTK_LIST_BOX(gen_box), GTK_WIDGET(default_row));

    gtk_box_append(GTK_BOX(page), gen_box);

    /* === PRIVACY SECTION === */
    GtkWidget *priv_label = gtk_label_new("Privacy & Security");
    gtk_widget_add_css_class(priv_label, "title-4");
    gtk_label_set_xalign(GTK_LABEL(priv_label), 0);
    gtk_widget_set_margin_top(priv_label, 16);
    gtk_box_append(GTK_BOX(page), priv_label);

    GtkWidget *priv_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(priv_box), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(priv_box, "boxed-list");

    /* Toggle rows helper macro - with signal handler and icon */
    #define ADD_TOGGLE(listbox, label_text, value, key_str, icon_name) { \
        AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new()); \
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), label_text); \
        GtkWidget *prefix_icon = gtk_image_new_from_icon_name(icon_name); \
        adw_action_row_add_prefix(row, prefix_icon); \
        GtkWidget *sw = gtk_switch_new(); \
        gtk_switch_set_active(GTK_SWITCH(sw), value); \
        gtk_widget_set_valign(sw, GTK_ALIGN_CENTER); \
        g_object_set_data(G_OBJECT(sw), "setting-key", (gpointer)key_str); \
        g_signal_connect(sw, "notify::active", G_CALLBACK(on_toggle_changed), self); \
        adw_action_row_add_suffix(row, sw); \
        adw_action_row_set_activatable_widget(row, sw); \
        gtk_list_box_append(GTK_LIST_BOX(listbox), GTK_WIDGET(row)); \
    }

    ADD_TOGGLE(priv_box, "Block Trackers", self->block_trackers, "block_trackers", "security-high-symbolic");
    ADD_TOGGLE(priv_box, "Block Ads", self->block_ads, "block_ads", "edit-clear-symbolic");
    ADD_TOGGLE(priv_box, "HTTPS Only", self->https_only, "https_only", "channel-secure-symbolic");
    ADD_TOGGLE(priv_box, "Block Popups", self->block_popups, "block_popups", "window-close-symbolic");
    ADD_TOGGLE(priv_box, "Do Not Track", self->do_not_track, "do_not_track", "view-conceal-symbolic");
    ADD_TOGGLE(priv_box, "Block Third-Party Cookies", self->block_third_party_cookies, "block_third_party_cookies", "dialog-password-symbolic");

    gtk_box_append(GTK_BOX(page), priv_box);

    /* === PERFORMANCE SECTION === */
    GtkWidget *perf_label = gtk_label_new("Performance");
    gtk_widget_add_css_class(perf_label, "title-4");
    gtk_label_set_xalign(GTK_LABEL(perf_label), 0);
    gtk_widget_set_margin_top(perf_label, 16);
    gtk_box_append(GTK_BOX(page), perf_label);

    GtkWidget *perf_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(perf_box), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(perf_box, "boxed-list");

    ADD_TOGGLE(perf_box, "Hardware Acceleration", self->hardware_acceleration, "hardware_acceleration", "video-display-symbolic");
    ADD_TOGGLE(perf_box, "Lazy Loading", self->lazy_loading, "lazy_loading", "emblem-synchronizing-symbolic");
    ADD_TOGGLE(perf_box, "Memory Saver", self->memory_saver, "memory_saver", "drive-harddisk-symbolic");

    #undef ADD_TOGGLE

    gtk_box_append(GTK_BOX(page), perf_box);

    /* === CLEAR DATA SECTION === */
    GtkWidget *data_label = gtk_label_new("Clear Data");
    gtk_widget_add_css_class(data_label, "title-4");
    gtk_label_set_xalign(GTK_LABEL(data_label), 0);
    gtk_widget_set_margin_top(data_label, 16);
    gtk_box_append(GTK_BOX(page), data_label);

    GtkWidget *data_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(data_box), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(data_box, "boxed-list");

    /* Clear Cookies */
    AdwActionRow *cookies_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(cookies_row), "Cookies");
    adw_action_row_set_subtitle(cookies_row, "Clear all saved cookies and sessions");
    GtkWidget *cookies_icon = gtk_image_new_from_icon_name("dialog-password-symbolic");
    adw_action_row_add_prefix(cookies_row, cookies_icon);
    GtkWidget *cookies_btn = gtk_button_new_with_label("Clear");
    gtk_widget_set_valign(cookies_btn, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(cookies_btn, "destructive-action");
    g_signal_connect(cookies_btn, "clicked", G_CALLBACK(on_clear_cookies_clicked), NULL);
    adw_action_row_add_suffix(cookies_row, cookies_btn);
    gtk_list_box_append(GTK_LIST_BOX(data_box), GTK_WIDGET(cookies_row));

    /* Clear Cache */
    AdwActionRow *cache_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(cache_row), "Cache");
    adw_action_row_set_subtitle(cache_row, "Clear cached pages and files");
    GtkWidget *cache_icon = gtk_image_new_from_icon_name("folder-symbolic");
    adw_action_row_add_prefix(cache_row, cache_icon);
    GtkWidget *cache_btn = gtk_button_new_with_label("Clear");
    gtk_widget_set_valign(cache_btn, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(cache_btn, "destructive-action");
    g_signal_connect(cache_btn, "clicked", G_CALLBACK(on_clear_cache_clicked), NULL);
    adw_action_row_add_suffix(cache_row, cache_btn);
    gtk_list_box_append(GTK_LIST_BOX(data_box), GTK_WIDGET(cache_row));

    /* Clear History */
    AdwActionRow *hist_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(hist_row), "History");
    adw_action_row_set_subtitle(hist_row, "Clear all browsing history");
    GtkWidget *hist_icon = gtk_image_new_from_icon_name("document-open-recent-symbolic");
    adw_action_row_add_prefix(hist_row, hist_icon);
    GtkWidget *hist_btn = gtk_button_new_with_label("Clear");
    gtk_widget_set_valign(hist_btn, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(hist_btn, "destructive-action");
    g_signal_connect(hist_btn, "clicked", G_CALLBACK(on_clear_history_clicked), NULL);
    adw_action_row_add_suffix(hist_row, hist_btn);
    gtk_list_box_append(GTK_LIST_BOX(data_box), GTK_WIDGET(hist_row));

    /* Clear All Data */
    AdwActionRow *all_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(all_row), "All Data");
    adw_action_row_set_subtitle(all_row, "Clear cookies, cache, history, bookmarks, and passwords");
    GtkWidget *all_icon = gtk_image_new_from_icon_name("edit-delete-symbolic");
    adw_action_row_add_prefix(all_row, all_icon);
    GtkWidget *all_btn = gtk_button_new_with_label("Clear All");
    gtk_widget_set_valign(all_btn, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(all_btn, "destructive-action");
    g_signal_connect(all_btn, "clicked", G_CALLBACK(on_clear_all_data_clicked), NULL);
    adw_action_row_add_suffix(all_row, all_btn);
    gtk_list_box_append(GTK_LIST_BOX(data_box), GTK_WIDGET(all_row));

    gtk_box_append(GTK_BOX(page), data_box);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), page);
    return scrolled;
}
