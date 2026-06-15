#include "settings-manager.h"

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
        case SEARCH_GOOGLE:     return "https://www.google.com/search?q=%s";
        case SEARCH_DUCKDUCKGO: return "https://duckduckgo.com/?q=%s";
        case SEARCH_BING:       return "https://www.bing.com/search?q=%s";
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

GtkWidget *
settings_manager_create_page(SettingsManager *self)
{
    GtkWidget *scrolled = gtk_scrolled_window_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);
    gtk_widget_add_css_class(box, "settings-page");

    /* Title */
    GtkWidget *title = gtk_label_new("Settings");
    gtk_widget_add_css_class(title, "title-1");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_append(GTK_BOX(box), title);

    /* General group */
    GtkWidget *general_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(general_group), "General");

    AdwActionRow *homepage_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(homepage_row), "Homepage");
    adw_action_row_set_subtitle(homepage_row, self->homepage);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(general_group), GTK_WIDGET(homepage_row));

    /* Search Engine selector */
    AdwActionRow *search_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(search_row), "Search Engine");
    adw_action_row_set_subtitle(search_row, "Choose your default search engine");

    GtkWidget *search_dropdown = gtk_drop_down_new_from_strings(
        (const char *[]){
            "Google", "DuckDuckGo", "Bing", "Brave Search",
            "Startpage", "Yahoo", "Yandex", NULL
        });
    gtk_drop_down_set_selected(GTK_DROP_DOWN(search_dropdown), self->search_engine);
    gtk_widget_set_valign(search_dropdown, GTK_ALIGN_CENTER);
    g_signal_connect(search_dropdown, "notify::selected",
        G_CALLBACK(on_search_engine_changed), self);
    adw_action_row_add_suffix(search_row, search_dropdown);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(general_group), GTK_WIDGET(search_row));

    gtk_box_append(GTK_BOX(box), general_group);

    /* Privacy group */
    GtkWidget *privacy_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(privacy_group), "Privacy & Security");

    AdwActionRow *tracker_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(tracker_row), "Block Trackers");
    GtkWidget *tracker_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(tracker_switch), self->block_trackers);
    gtk_widget_set_valign(tracker_switch, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(tracker_row, tracker_switch);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(privacy_group), GTK_WIDGET(tracker_row));

    AdwActionRow *ads_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(ads_row), "Block Ads");
    GtkWidget *ads_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(ads_switch), self->block_ads);
    gtk_widget_set_valign(ads_switch, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ads_row, ads_switch);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(privacy_group), GTK_WIDGET(ads_row));

    AdwActionRow *https_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(https_row), "HTTPS Only");
    GtkWidget *https_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(https_switch), self->https_only);
    gtk_widget_set_valign(https_switch, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(https_row, https_switch);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(privacy_group), GTK_WIDGET(https_row));

    AdwActionRow *popup_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(popup_row), "Block Popups");
    GtkWidget *popup_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(popup_switch), self->block_popups);
    gtk_widget_set_valign(popup_switch, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(popup_row, popup_switch);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(privacy_group), GTK_WIDGET(popup_row));

    AdwActionRow *dnt_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(dnt_row), "Do Not Track");
    GtkWidget *dnt_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(dnt_switch), self->do_not_track);
    gtk_widget_set_valign(dnt_switch, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(dnt_row, dnt_switch);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(privacy_group), GTK_WIDGET(dnt_row));

    AdwActionRow *cookies_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(cookies_row), "Block Third-Party Cookies");
    GtkWidget *cookies_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(cookies_switch), self->block_third_party_cookies);
    gtk_widget_set_valign(cookies_switch, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(cookies_row, cookies_switch);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(privacy_group), GTK_WIDGET(cookies_row));

    gtk_box_append(GTK_BOX(box), privacy_group);

    /* Performance group */
    GtkWidget *perf_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(perf_group), "Performance");

    AdwActionRow *hw_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(hw_row), "Hardware Acceleration");
    GtkWidget *hw_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(hw_switch), self->hardware_acceleration);
    gtk_widget_set_valign(hw_switch, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(hw_row, hw_switch);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(perf_group), GTK_WIDGET(hw_row));

    AdwActionRow *lazy_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(lazy_row), "Lazy Loading");
    GtkWidget *lazy_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(lazy_switch), self->lazy_loading);
    gtk_widget_set_valign(lazy_switch, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(lazy_row, lazy_switch);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(perf_group), GTK_WIDGET(lazy_row));

    AdwActionRow *mem_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(mem_row), "Memory Saver");
    GtkWidget *mem_switch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(mem_switch), self->memory_saver);
    gtk_widget_set_valign(mem_switch, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(mem_row, mem_switch);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(perf_group), GTK_WIDGET(mem_row));

    gtk_box_append(GTK_BOX(box), perf_group);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), box);
    return scrolled;
}
