#include "browser-window.h"
#include "browser-tab.h"
#include "download-manager.h"
#include "bookmark-manager.h"
#include "history-manager.h"
#include "settings-manager.h"
#include "password-manager.h"
#include <string.h>

struct _BrowserWindow {
    AdwApplicationWindow parent_instance;

    /* Header / Toolbar */
    GtkWidget *header_bar;
    GtkWidget *url_entry;
    GtkWidget *back_btn;
    GtkWidget *forward_btn;
    GtkWidget *reload_btn;
    GtkWidget *home_btn;

    /* Tab system */
    GtkWidget *tab_bar_box;
    GtkWidget *tab_stack;
    GtkWidget *new_tab_btn;
    GPtrArray *tabs;
    int current_tab;
    guint tab_id_counter;  /* unique ID for tab naming */

    /* Sidebar */
    GtkWidget *sidebar_revealer;
    GtkWidget *sidebar_stack;

    /* Tab panel */
    GtkWidget *tab_panel_revealer;

    /* Loading indicator */
    GtkWidget *progress_bar;

    /* Download notification bar */
    GtkWidget *download_bar;
    GtkWidget *download_bar_label;
    GtkWidget *download_bar_progress;
    guint download_pulse_id;

    /* State */
    gboolean is_fullscreen;
};

G_DEFINE_TYPE(BrowserWindow, browser_window, ADW_TYPE_APPLICATION_WINDOW)

/* Forward declarations */
static void update_navigation_buttons(BrowserWindow *self);
static void update_tab_bar(BrowserWindow *self);
static void on_tab_title_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data);
static void on_tab_uri_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data);
static void on_tab_load_changed(WebKitWebView *web_view, WebKitLoadEvent event, gpointer user_data);
static void on_tab_progress_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data);

/* URL entry activation */
static void
on_url_entry_activate(GtkEntry *entry, gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (text && *text) {
        browser_window_navigate(self, text);
    }
}

/* Navigation button callbacks */
static void
on_back_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    browser_window_go_back(BROWSER_WINDOW(user_data));
}

static void
on_forward_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    browser_window_go_forward(BROWSER_WINDOW(user_data));
}

static void
on_reload_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    browser_window_reload(BROWSER_WINDOW(user_data));
}

static void
on_home_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* Load startup page in current tab */
    if (self->current_tab >= 0 && self->current_tab < (int)self->tabs->len) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
        WebKitWebView *web_view = browser_tab_get_web_view(tab);

        /* Load startup HTML from GResource */
        GBytes *bytes = g_resources_lookup_data("/com/openbrowser/startup.html",
            G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
        if (bytes) {
            gsize size;
            const char *data = g_bytes_get_data(bytes, &size);
            webkit_web_view_load_html(web_view, data, "https://unpkg.com");
            g_bytes_unref(bytes);
        }
        gtk_editable_set_text(GTK_EDITABLE(self->url_entry), "");
    }
}

static void
on_new_tab_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    browser_window_new_tab(BROWSER_WINDOW(user_data), NULL);
}

static void
on_go_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    const char *text = gtk_editable_get_text(GTK_EDITABLE(self->url_entry));
    if (text && *text) {
        browser_window_navigate(self, text);
    }
}

static void
on_toggle_panel_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    gboolean visible = gtk_revealer_get_reveal_child(GTK_REVEALER(self->tab_panel_revealer));
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->tab_panel_revealer), !visible);
}

/* Tab switching */
static void
on_tab_clicked_gesture(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
    (void)n_press; (void)x; (void)y;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(gesture), "tab-index"));
    browser_window_set_tab(self, index);
}

/* Tab close button */
static void
on_tab_close_clicked(GtkButton *button, gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    int index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "tab-index"));
    browser_window_close_tab(self, index);
}

/* Menu actions */
static void
on_new_tab_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    browser_window_new_tab(BROWSER_WINDOW(user_data), NULL);
}

static void
on_new_private_tab_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    browser_window_new_private_tab(BROWSER_WINDOW(user_data));
}

static void
on_close_tab_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    browser_window_close_tab(self, self->current_tab);
}

static void
on_find_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    browser_window_find_in_page(self, NULL);
}

static void
on_zoom_in_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    browser_window_zoom_in(BROWSER_WINDOW(user_data));
}

static void
on_zoom_out_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    browser_window_zoom_out(BROWSER_WINDOW(user_data));
}

static void
on_zoom_reset_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    browser_window_zoom_reset(BROWSER_WINDOW(user_data));
}

static void
on_fullscreen_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    browser_window_toggle_fullscreen(BROWSER_WINDOW(user_data));
}

static void
on_sidebar_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    browser_window_toggle_sidebar(BROWSER_WINDOW(user_data));
}

static void
on_bookmark_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    const char *uri = browser_tab_get_uri(tab);
    const char *title = browser_tab_get_title(tab);

    /* Don't bookmark internal/startup pages */
    if (!uri || !*uri) return;
    if (g_str_has_prefix(uri, "about:")) return;
    if (g_strcmp0(uri, "https://unpkg.com") == 0) return;
    if (g_strcmp0(uri, "https://unpkg.com/") == 0) return;

    BookmarkManager *bm = bookmark_manager_get_default();
    if (bookmark_manager_exists(bm, uri)) {
        bookmark_manager_remove(bm, uri);
        /* Show toast - removed */
        AdwToast *toast = adw_toast_new("Bookmark removed");
        adw_toast_set_timeout(toast, 2);
        adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(
            g_object_get_data(G_OBJECT(self), "toast-overlay")), toast);
    } else {
        bookmark_manager_add(bm, title, uri, "Unsorted");
        /* Show toast - added */
        AdwToast *toast = adw_toast_new("★ Page bookmarked!");
        adw_toast_set_timeout(toast, 2);
        adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(
            g_object_get_data(G_OBJECT(self), "toast-overlay")), toast);
    }
}

static void
on_downloads_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* Check if downloads tab already exists - switch to it */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        const char *custom = g_object_get_data(G_OBJECT(tab), "custom-title");
        if (custom && g_strcmp0(custom, "Downloads") == 0) {
            browser_window_set_tab(self, i);
            return;
        }
    }

    /* Create a full-page downloads tab */
    BrowserTab *tab = browser_tab_new("about:downloads", FALSE);
    g_object_set_data_full(G_OBJECT(tab), "custom-title", g_strdup("Downloads"), g_free);
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    DownloadManager *dm = download_manager_get_default();
    GtkWidget *downloads_page = download_manager_create_full_page(dm);

    char *name = g_strdup_printf("tab-id-%u", self->tab_id_counter++);
    gtk_stack_add_named(GTK_STACK(self->tab_stack), downloads_page, name);
    g_object_set_data_full(G_OBJECT(tab), "stack-name", name, g_free);
    g_object_set_data(G_OBJECT(tab), "settings-page", downloads_page);

    int index = self->tabs->len - 1;
    self->current_tab = index;
    gtk_stack_set_visible_child(GTK_STACK(self->tab_stack), downloads_page);
    gtk_editable_set_text(GTK_EDITABLE(self->url_entry), "about:downloads");
    gtk_window_set_title(GTK_WINDOW(self), "Downloads - OpenBrowser");
    update_tab_bar(self);
}

static void
on_history_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* Check if history tab already exists */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        const char *custom = g_object_get_data(G_OBJECT(tab), "custom-title");
        if (custom && g_strcmp0(custom, "History") == 0) {
            browser_window_set_tab(self, i);
            return;
        }
    }

    BrowserTab *tab = browser_tab_new("about:history", FALSE);
    g_object_set_data_full(G_OBJECT(tab), "custom-title", g_strdup("History"), g_free);
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    HistoryManager *hm = history_manager_get_default();
    GtkWidget *history_page = history_manager_create_full_page(hm);

    char *name = g_strdup_printf("tab-id-%u", self->tab_id_counter++);
    gtk_stack_add_named(GTK_STACK(self->tab_stack), history_page, name);
    g_object_set_data_full(G_OBJECT(tab), "stack-name", name, g_free);
    g_object_set_data(G_OBJECT(tab), "settings-page", history_page);

    int index = self->tabs->len - 1;
    self->current_tab = index;
    gtk_stack_set_visible_child(GTK_STACK(self->tab_stack), history_page);
    gtk_editable_set_text(GTK_EDITABLE(self->url_entry), "about:history");
    gtk_window_set_title(GTK_WINDOW(self), "History - OpenBrowser");
    update_tab_bar(self);
}

static void
on_bookmarks_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* Check if bookmarks tab already exists */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        const char *custom = g_object_get_data(G_OBJECT(tab), "custom-title");
        if (custom && g_strcmp0(custom, "Bookmarks") == 0) {
            browser_window_set_tab(self, i);
            return;
        }
    }

    BrowserTab *tab = browser_tab_new("about:bookmarks", FALSE);
    g_object_set_data_full(G_OBJECT(tab), "custom-title", g_strdup("Bookmarks"), g_free);
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    BookmarkManager *bm = bookmark_manager_get_default();
    GtkWidget *bookmarks_page = bookmark_manager_create_full_page(bm);

    char *name = g_strdup_printf("tab-id-%u", self->tab_id_counter++);
    gtk_stack_add_named(GTK_STACK(self->tab_stack), bookmarks_page, name);
    g_object_set_data_full(G_OBJECT(tab), "stack-name", name, g_free);
    g_object_set_data(G_OBJECT(tab), "settings-page", bookmarks_page);

    int index = self->tabs->len - 1;
    self->current_tab = index;
    gtk_stack_set_visible_child(GTK_STACK(self->tab_stack), bookmarks_page);
    gtk_editable_set_text(GTK_EDITABLE(self->url_entry), "about:bookmarks");
    gtk_window_set_title(GTK_WINDOW(self), "Bookmarks - OpenBrowser");
    update_tab_bar(self);
}

static void
on_print_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    WebKitWebView *web_view = browser_tab_get_web_view(tab);
    WebKitPrintOperation *print_op = webkit_print_operation_new(web_view);
    webkit_print_operation_run_dialog(print_op, GTK_WINDOW(self));
    g_object_unref(print_op);
}

static void
on_devtools_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    WebKitWebView *web_view = browser_tab_get_web_view(tab);
    WebKitWebInspector *inspector = webkit_web_view_get_inspector(web_view);
    webkit_web_inspector_show(inspector);
}

static void
on_settings_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* Check if settings tab already exists */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        const char *custom = g_object_get_data(G_OBJECT(tab), "custom-title");
        if (custom && g_strcmp0(custom, "Settings") == 0) {
            browser_window_set_tab(self, i);
            return;
        }
    }

    SettingsManager *sm = settings_manager_get_default();
    GtkWidget *settings_page = settings_manager_create_page(sm);

    BrowserTab *tab = browser_tab_new("about:settings", FALSE);
    g_object_set_data_full(G_OBJECT(tab), "custom-title", g_strdup("Settings"), g_free);
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    char *name = g_strdup_printf("tab-id-%u", self->tab_id_counter++);
    gtk_stack_add_named(GTK_STACK(self->tab_stack), settings_page, name);
    g_object_set_data_full(G_OBJECT(tab), "stack-name", name, g_free);
    g_object_set_data(G_OBJECT(tab), "settings-page", settings_page);

    int index = self->tabs->len - 1;
    self->current_tab = index;
    gtk_stack_set_visible_child(GTK_STACK(self->tab_stack), settings_page);

    gtk_editable_set_text(GTK_EDITABLE(self->url_entry), "about:settings");
    gtk_window_set_title(GTK_WINDOW(self), "Settings - OpenBrowser");
    update_tab_bar(self);
}

/* Menu creation */
static GtkWidget *
create_menu_button(BrowserWindow *self)
{
    (void)self;
    GtkWidget *button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button), "open-menu-symbolic");
    gtk_widget_add_css_class(button, "flat");

    GMenu *menu = g_menu_new();

    /* Tab section */
    GMenu *tab_section = g_menu_new();
    g_menu_append(tab_section, "New Tab", "win.new-tab");
    g_menu_append(tab_section, "New Private Tab", "win.new-private-tab");
    g_menu_append(tab_section, "Close Tab", "win.close-tab");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(tab_section));
    g_object_unref(tab_section);

    /* View section */
    GMenu *view_section = g_menu_new();
    g_menu_append(view_section, "Zoom In", "win.zoom-in");
    g_menu_append(view_section, "Zoom Out", "win.zoom-out");
    g_menu_append(view_section, "Reset Zoom", "win.zoom-reset");
    g_menu_append(view_section, "Full Screen", "win.fullscreen");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(view_section));
    g_object_unref(view_section);

    /* Tools section */
    GMenu *tools_section = g_menu_new();
    g_menu_append(tools_section, "Find in Page", "win.find");
    g_menu_append(tools_section, "Print", "win.print");
    g_menu_append(tools_section, "Developer Tools", "win.devtools");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(tools_section));
    g_object_unref(tools_section);

    /* Pages section */
    GMenu *pages_section = g_menu_new();
    g_menu_append(pages_section, "Downloads", "win.downloads");
    g_menu_append(pages_section, "Bookmarks", "win.show-bookmarks");
    g_menu_append(pages_section, "History", "win.history");
    g_menu_append(pages_section, "Passwords", "win.passwords");
    g_menu_append(pages_section, "Settings", "win.settings");
    g_menu_append(pages_section, "About", "win.about");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(pages_section));
    g_object_unref(pages_section);

    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(button), G_MENU_MODEL(menu));
    g_object_unref(menu);

    return button;
}

/* Keyboard shortcuts */
static void
setup_shortcuts(BrowserWindow *self)
{
    GtkEventController *controller = gtk_shortcut_controller_new();
    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(controller), GTK_SHORTCUT_SCOPE_MANAGED);

    /* Ctrl+T - New Tab */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>t"),
            gtk_named_action_new("win.new-tab")
        ));

    /* Ctrl+W - Close Tab */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>w"),
            gtk_named_action_new("win.close-tab")
        ));

    /* Ctrl+F - Find */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>f"),
            gtk_named_action_new("win.find")
        ));

    /* Ctrl+Plus - Zoom In */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>plus"),
            gtk_named_action_new("win.zoom-in")
        ));

    /* Ctrl+Minus - Zoom Out */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>minus"),
            gtk_named_action_new("win.zoom-out")
        ));

    /* Ctrl+0 - Reset Zoom */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>0"),
            gtk_named_action_new("win.zoom-reset")
        ));

    /* F11 - Fullscreen */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("F11"),
            gtk_named_action_new("win.fullscreen")
        ));

    /* Ctrl+Shift+I - DevTools */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control><Shift>i"),
            gtk_named_action_new("win.devtools")
        ));

    /* Ctrl+D - Bookmark */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>d"),
            gtk_named_action_new("win.bookmark")
        ));

    /* Ctrl+P - Print */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>p"),
            gtk_named_action_new("win.print")
        ));

    /* Ctrl+H - History */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>h"),
            gtk_named_action_new("win.history")
        ));

    /* Ctrl+L - Focus URL bar (handled via action) */
    /* Note: URL focus is handled by Ctrl+L keybinding in the entry itself */

    /* Ctrl+R - Reload */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>r"),
            gtk_named_action_new("win.reload")
        ));

    /* F5 - Reload */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("F5"),
            gtk_named_action_new("win.reload")
        ));

    /* Ctrl+L - Focus URL bar */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>l"),
            gtk_named_action_new("win.focus-url")
        ));

    /* Ctrl+Tab - Next Tab */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>Tab"),
            gtk_named_action_new("win.next-tab")
        ));

    /* Ctrl+Shift+Tab - Previous Tab */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control><Shift>Tab"),
            gtk_named_action_new("win.prev-tab")
        ));

    /* Ctrl+Page_Down - Next Tab */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>Page_Down"),
            gtk_named_action_new("win.next-tab")
        ));

    /* Ctrl+Page_Up - Previous Tab */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>Page_Up"),
            gtk_named_action_new("win.prev-tab")
        ));

    /* Alt+Left - Back */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Alt>Left"),
            gtk_named_action_new("win.reload") /* reuse; will add back action */
        ));

    gtk_widget_add_controller(GTK_WIDGET(self), controller);
}

/* Sidebar creation */
static GtkWidget *
create_sidebar(BrowserWindow *self)
{
    GtkWidget *revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer), GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), FALSE);

    GtkWidget *sidebar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sidebar_box, "sidebar");
    gtk_widget_set_size_request(sidebar_box, 280, -1);

    /* Sidebar header with close button */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(header, "sidebar-header");

    GtkWidget *header_label = gtk_label_new("Panel");
    gtk_widget_set_hexpand(header_label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(header_label), 0);
    gtk_box_append(GTK_BOX(header), header_label);

    GtkWidget *close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(close_btn, "nav-button");
    g_signal_connect_swapped(close_btn, "clicked",
        G_CALLBACK(browser_window_toggle_sidebar), self);
    gtk_box_append(GTK_BOX(header), close_btn);

    gtk_box_append(GTK_BOX(sidebar_box), header);

    /* Sidebar stack */
    self->sidebar_stack = gtk_stack_new();
    gtk_widget_set_vexpand(self->sidebar_stack, TRUE);

    /* Bookmarks panel */
    BookmarkManager *bm = bookmark_manager_get_default();
    GtkWidget *bookmarks_widget = bookmark_manager_create_widget(bm);
    gtk_stack_add_named(GTK_STACK(self->sidebar_stack), bookmarks_widget, "bookmarks");

    /* History panel */
    HistoryManager *hm = history_manager_get_default();
    GtkWidget *history_widget = history_manager_create_widget(hm);
    gtk_stack_add_named(GTK_STACK(self->sidebar_stack), history_widget, "history");

    /* Downloads panel */
    DownloadManager *dm = download_manager_get_default();
    GtkWidget *downloads_widget = download_manager_create_widget(dm);
    gtk_stack_add_named(GTK_STACK(self->sidebar_stack), downloads_widget, "downloads");

    gtk_box_append(GTK_BOX(sidebar_box), self->sidebar_stack);
    gtk_revealer_set_child(GTK_REVEALER(revealer), sidebar_box);

    return revealer;
}

/* Download progress update callback */
static void
on_active_download_progress(WebKitDownload *download, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    double progress = webkit_download_get_estimated_progress(download);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->download_bar_progress), progress);

    const char *dest = webkit_download_get_destination(download);
    if (dest) {
        char *basename = g_path_get_basename(dest);
        char *text = g_strdup_printf("Downloading: %s (%.0f%%)", basename, progress * 100);
        gtk_label_set_text(GTK_LABEL(self->download_bar_label), text);
        g_free(text);
        g_free(basename);
    }
}

static gboolean
hide_download_bar_timeout(gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->download_bar), FALSE);
    return G_SOURCE_REMOVE;
}

static void
on_active_download_finished(WebKitDownload *download, gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    const char *dest = webkit_download_get_destination(download);

    if (dest) {
        char *basename = g_path_get_basename(dest);
        char *text = g_strdup_printf("Downloaded: %s", basename);
        gtk_label_set_text(GTK_LABEL(self->download_bar_label), text);
        g_free(text);
        g_free(basename);
    }
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->download_bar_progress), 1.0);

    /* Hide bar after 4 seconds */
    g_timeout_add_seconds(4, hide_download_bar_timeout, self);
}

static gboolean
on_download_decide_dest(WebKitDownload *download, const char *suggested_filename, gpointer user_data)
{
    (void)user_data;

    /* Check if "Save As" path was set (from file dialog) */
    WebKitWebView *web_view = webkit_download_get_web_view(download);
    GtkWidget *toplevel = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(web_view)));
    char *save_as_path = NULL;

    if (toplevel && BROWSER_IS_WINDOW(toplevel)) {
        save_as_path = g_object_get_data(G_OBJECT(toplevel), "save-as-path");
        if (save_as_path) {
            webkit_download_set_allow_overwrite(download, TRUE);
            webkit_download_set_destination(download, save_as_path);
            g_print("[OpenBrowser] Save As to: %s\n", save_as_path);
            /* Clear the save-as-path so next download goes to default */
            g_object_set_data(G_OBJECT(toplevel), "save-as-path", NULL);
            return TRUE;
        }
    }

    /* Default: save to Downloads folder */
    const char *download_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (!download_dir) download_dir = g_get_home_dir();

    char *destination = g_build_filename(download_dir, suggested_filename, NULL);

    /* Avoid overwriting - add number if file exists */
    if (g_file_test(destination, G_FILE_TEST_EXISTS)) {
        char *name = g_strdup(suggested_filename);
        char *ext = strrchr(name, '.');
        char *base = NULL;
        char *extension = NULL;
        if (ext) {
            extension = g_strdup(ext);
            *ext = '\0';
            base = g_strdup(name);
        } else {
            base = g_strdup(name);
            extension = g_strdup("");
        }
        g_free(name);

        int counter = 1;
        g_free(destination);
        do {
            destination = g_strdup_printf("%s/%s (%d)%s", download_dir, base, counter, extension);
            counter++;
        } while (g_file_test(destination, G_FILE_TEST_EXISTS));

        g_free(base);
        g_free(extension);
    }

    webkit_download_set_allow_overwrite(download, FALSE);
    webkit_download_set_destination(download, destination);
    g_print("[OpenBrowser] Downloading to: %s\n", destination);

    g_free(destination);
    return TRUE;
}

static void
on_download_started(WebKitNetworkSession *session, WebKitDownload *download, gpointer user_data)
{
    (void)session;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    g_print("[OpenBrowser] Download started!\n");

    /* Connect decide-destination IMMEDIATELY */
    g_signal_connect(download, "decide-destination",
        G_CALLBACK(on_download_decide_dest), NULL);

    /* Connect progress and finished to update the bar */
    g_signal_connect(download, "notify::estimated-progress",
        G_CALLBACK(on_active_download_progress), self);
    g_signal_connect(download, "finished",
        G_CALLBACK(on_active_download_finished), self);

    /* Show the download bar with animation */
    gtk_label_set_text(GTK_LABEL(self->download_bar_label), "Starting download...");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->download_bar_progress), 0.0);
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->download_bar), TRUE);

    /* Add to download manager */
    DownloadManager *dm = download_manager_get_default();
    download_manager_add_download(dm, download);
}

/* Policy decision - handle downloads */
static gboolean
on_decide_policy(WebKitWebView *web_view, WebKitPolicyDecision *decision,
    WebKitPolicyDecisionType type, gpointer user_data)
{
    (void)web_view;
    (void)user_data;

    if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
        WebKitResponsePolicyDecision *response_decision = WEBKIT_RESPONSE_POLICY_DECISION(decision);
        if (!webkit_response_policy_decision_is_mime_type_supported(response_decision)) {
            webkit_policy_decision_download(decision);
            return TRUE;
        }
    }
    return FALSE;
}

/* Context menu - Save Image / Save Image As */
static void
on_save_image_activate(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    const char *image_uri = g_object_get_data(G_OBJECT(self), "context-image-uri");
    if (image_uri) {
        /* Download directly to ~/Downloads */
        if (self->current_tab >= 0 && self->current_tab < (int)self->tabs->len) {
            BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
            WebKitWebView *wv = browser_tab_get_web_view(tab);
            webkit_web_view_download_uri(wv, image_uri);
        }
    }
}

static void
on_save_image_as_callback(GObject *source, GAsyncResult *result, gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError *error = NULL;

    GFile *file = gtk_file_dialog_save_finish(dialog, result, &error);
    if (file) {
        const char *image_uri = g_object_get_data(G_OBJECT(self), "context-image-uri");
        if (image_uri) {
            /* Store the chosen path for the download destination */
            char *path = g_file_get_path(file);
            g_object_set_data_full(G_OBJECT(self), "save-as-path", path, g_free);

            /* Start download - our decide-destination handler will check for save-as-path */
            if (self->current_tab >= 0 && self->current_tab < (int)self->tabs->len) {
                BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
                WebKitWebView *wv = browser_tab_get_web_view(tab);
                webkit_web_view_download_uri(wv, image_uri);
            }
        }
        g_object_unref(file);
    }
    if (error) g_error_free(error);
}

static void
on_save_image_as_activate(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    const char *image_uri = g_object_get_data(G_OBJECT(self), "context-image-uri");
    if (!image_uri) return;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Save Image As");

    /* Set initial folder */
    const char *download_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (!download_dir) download_dir = g_get_home_dir();
    GFile *folder = g_file_new_for_path(download_dir);
    gtk_file_dialog_set_initial_folder(dialog, folder);
    g_object_unref(folder);

    /* Try to get filename from URI */
    char *basename = g_path_get_basename(image_uri);
    if (basename && *basename && g_strcmp0(basename, "/") != 0) {
        /* Remove query params */
        char *q = strchr(basename, '?');
        if (q) *q = '\0';
        gtk_file_dialog_set_initial_name(dialog, basename);
    } else {
        gtk_file_dialog_set_initial_name(dialog, "image.png");
    }
    g_free(basename);

    gtk_file_dialog_save(dialog, GTK_WINDOW(self), NULL,
        on_save_image_as_callback, self);
    g_object_unref(dialog);
}

static gboolean
on_context_menu(WebKitWebView *web_view, WebKitContextMenu *context_menu,
    WebKitHitTestResult *hit_test, gpointer user_data)
{
    (void)web_view;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    if (webkit_hit_test_result_context_is_image(hit_test)) {
        const char *image_uri = webkit_hit_test_result_get_image_uri(hit_test);
        g_object_set_data_full(G_OBJECT(self), "context-image-uri",
            g_strdup(image_uri), g_free);

        /* Remove WebKit's built-in "Save Image As" to avoid duplicate */
        GList *items = webkit_context_menu_get_items(context_menu);
        for (GList *l = items; l; l = l->next) {
            WebKitContextMenuItem *item = l->data;
            WebKitContextMenuAction stock_action = webkit_context_menu_item_get_stock_action(item);
            if (stock_action == WEBKIT_CONTEXT_MENU_ACTION_DOWNLOAD_IMAGE_TO_DISK) {
                webkit_context_menu_remove(context_menu, item);
                break;
            }
        }

        /* Add separator */
        webkit_context_menu_append(context_menu,
            webkit_context_menu_item_new_separator());

        /* Add "Save Image" */
        GSimpleAction *save_action = g_simple_action_new("save-image", NULL);
        g_signal_connect(save_action, "activate", G_CALLBACK(on_save_image_activate), self);
        g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(save_action));
        WebKitContextMenuItem *save_item = webkit_context_menu_item_new_from_gaction(
            G_ACTION(save_action), "Save Image", NULL);
        webkit_context_menu_append(context_menu, save_item);

        /* Add "Save Image As..." */
        GSimpleAction *save_as_action = g_simple_action_new("save-image-as", NULL);
        g_signal_connect(save_as_action, "activate", G_CALLBACK(on_save_image_as_activate), self);
        g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(save_as_action));
        WebKitContextMenuItem *save_as_item = webkit_context_menu_item_new_from_gaction(
            G_ACTION(save_as_action), "Save Image As...", NULL);
        webkit_context_menu_append(context_menu, save_as_item);
    }

    return FALSE;
}

/* Web view signals connection */
static void
connect_tab_signals(BrowserWindow *self, BrowserTab *tab)
{
    WebKitWebView *web_view = browser_tab_get_web_view(tab);

    g_signal_connect(web_view, "notify::title", G_CALLBACK(on_tab_title_changed), self);
    g_signal_connect(web_view, "notify::uri", G_CALLBACK(on_tab_uri_changed), self);
    g_signal_connect(web_view, "load-changed", G_CALLBACK(on_tab_load_changed), self);
    g_signal_connect(web_view, "notify::estimated-load-progress",
        G_CALLBACK(on_tab_progress_changed), self);
    g_signal_connect(web_view, "decide-policy",
        G_CALLBACK(on_decide_policy), self);
    g_signal_connect(web_view, "context-menu",
        G_CALLBACK(on_context_menu), self);
}

static void
on_tab_title_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* Find which tab this web_view belongs to */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        if (browser_tab_get_web_view(tab) == web_view) {
            if ((int)i == self->current_tab) {
                const char *title = webkit_web_view_get_title(web_view);
                if (title) {
                    char *window_title = g_strdup_printf("%s - OpenBrowser", title);
                    gtk_window_set_title(GTK_WINDOW(self), window_title);
                    g_free(window_title);
                }
            }
            update_tab_bar(self);
            break;
        }
    }
}

static void
on_tab_uri_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        if (browser_tab_get_web_view(tab) == web_view) {
            if ((int)i == self->current_tab) {
                const char *uri = webkit_web_view_get_uri(web_view);
                if (uri) {
                    gtk_editable_set_text(GTK_EDITABLE(self->url_entry), uri);
                }
                update_navigation_buttons(self);
            }

            /* Add to history */
            if (!browser_tab_is_private(tab)) {
                const char *uri = webkit_web_view_get_uri(web_view);
                const char *title = webkit_web_view_get_title(web_view);
                if (uri && title) {
                    HistoryManager *hm = history_manager_get_default();
                    history_manager_add(hm, title, uri);
                }
            }
            break;
        }
    }
}

static void
on_tab_load_changed(WebKitWebView *web_view, WebKitLoadEvent event, gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        if (browser_tab_get_web_view(tab) == web_view) {
            if ((int)i == self->current_tab) {
                switch (event) {
                    case WEBKIT_LOAD_STARTED:
                        gtk_widget_set_visible(self->progress_bar, TRUE);
                        gtk_button_set_icon_name(GTK_BUTTON(self->reload_btn), "process-stop-symbolic");
                        break;
                    case WEBKIT_LOAD_COMMITTED:
                        break;
                    case WEBKIT_LOAD_FINISHED:
                        gtk_widget_set_visible(self->progress_bar, FALSE);
                        gtk_button_set_icon_name(GTK_BUTTON(self->reload_btn), "view-refresh-symbolic");
                        update_navigation_buttons(self);
                        break;
                    default:
                        break;
                }
            }
            break;
        }
    }
}

static void
on_tab_progress_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        if (browser_tab_get_web_view(tab) == web_view) {
            if ((int)i == self->current_tab) {
                double progress = webkit_web_view_get_estimated_load_progress(web_view);
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->progress_bar), progress);
            }
            break;
        }
    }
}

static void
update_navigation_buttons(BrowserWindow *self)
{
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    WebKitWebView *web_view = browser_tab_get_web_view(tab);

    gtk_widget_set_sensitive(self->back_btn, webkit_web_view_can_go_back(web_view));
    gtk_widget_set_sensitive(self->forward_btn, webkit_web_view_can_go_forward(web_view));
}

static void
update_tab_bar(BrowserWindow *self)
{
    /* Clear all children from tab_bar_box */
    GtkWidget *child = gtk_widget_get_first_child(self->tab_bar_box);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        gtk_box_remove(GTK_BOX(self->tab_bar_box), child);
        child = next;
    }

    /* Recreate tab items as vertical list */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);

        /* Check for custom title (Downloads, Bookmarks, Settings) */
        const char *custom_title = g_object_get_data(G_OBJECT(tab), "custom-title");
        const char *title = custom_title ? custom_title : browser_tab_get_title(tab);

        /* Tab row: icon + title + close */
        GtkWidget *tab_widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_add_css_class(tab_widget, "tab-item");
        if ((int)i == self->current_tab) {
            gtk_widget_add_css_class(tab_widget, "tab-item-active");
        }

        /* Favicon placeholder */
        const char *icon_name = browser_tab_is_private(tab) ?
            "security-high-symbolic" : "document-open-symbolic";
        GtkWidget *icon = gtk_image_new_from_icon_name(icon_name);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
        gtk_box_append(GTK_BOX(tab_widget), icon);

        /* Title */
        GtkWidget *label = gtk_label_new(title);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_widget_set_hexpand(label, TRUE);
        gtk_box_append(GTK_BOX(tab_widget), label);

        /* Close button */
        if (!browser_tab_is_pinned(tab)) {
            GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic");
            gtk_widget_add_css_class(close, "flat");
            gtk_widget_add_css_class(close, "circular");
            gtk_widget_add_css_class(close, "tab-close-btn");
            g_object_set_data(G_OBJECT(close), "tab-index", GINT_TO_POINTER(i));
            g_signal_connect(close, "clicked", G_CALLBACK(on_tab_close_clicked), self);
            gtk_box_append(GTK_BOX(tab_widget), close);
        }

        /* Click gesture on the tab row */
        GtkGesture *click = gtk_gesture_click_new();
        g_object_set_data(G_OBJECT(click), "tab-index", GINT_TO_POINTER(i));
        g_signal_connect(click, "released", G_CALLBACK(on_tab_clicked_gesture), self);
        gtk_widget_add_controller(tab_widget, GTK_EVENT_CONTROLLER(click));

        gtk_box_append(GTK_BOX(self->tab_bar_box), tab_widget);
    }
}

static void
on_reload_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    browser_window_reload(BROWSER_WINDOW(user_data));
}

static void
on_focus_url_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    gtk_widget_grab_focus(self->url_entry);
    gtk_editable_select_region(GTK_EDITABLE(self->url_entry), 0, -1);
}

static void
on_next_tab_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    if (self->tabs->len == 0) return;
    int next = (self->current_tab + 1) % self->tabs->len;
    browser_window_set_tab(self, next);
}

static void
on_prev_tab_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    if (self->tabs->len == 0) return;
    int prev = (self->current_tab - 1 + self->tabs->len) % self->tabs->len;
    browser_window_set_tab(self, prev);
}

static void
on_about_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* Check if about tab already exists */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        const char *custom = g_object_get_data(G_OBJECT(tab), "custom-title");
        if (custom && g_strcmp0(custom, "About") == 0) {
            browser_window_set_tab(self, i);
            return;
        }
    }

    /* Load about page from GResource */
    BrowserTab *tab = browser_tab_new("data:text/html,", FALSE);
    g_object_set_data_full(G_OBJECT(tab), "custom-title", g_strdup("About"), g_free);
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    /* Load about HTML into the tab's webview */
    WebKitWebView *wv = browser_tab_get_web_view(tab);
    GBytes *bytes = g_resources_lookup_data("/com/openbrowser/about.html",
        G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
    if (bytes) {
        gsize size;
        const char *data = g_bytes_get_data(bytes, &size);
        webkit_web_view_load_html(wv, data, NULL);
        g_bytes_unref(bytes);
    }

    char *name = g_strdup_printf("tab-id-%u", self->tab_id_counter++);
    gtk_stack_add_named(GTK_STACK(self->tab_stack), GTK_WIDGET(tab), name);
    g_object_set_data_full(G_OBJECT(tab), "stack-name", name, g_free);

    int index = self->tabs->len - 1;
    self->current_tab = index;
    gtk_stack_set_visible_child(GTK_STACK(self->tab_stack), GTK_WIDGET(tab));
    gtk_editable_set_text(GTK_EDITABLE(self->url_entry), "about:openbrowser");
    gtk_window_set_title(GTK_WINDOW(self), "About - OpenBrowser");
    update_tab_bar(self);
}

static void
on_passwords_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* Check if passwords tab already exists */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        const char *custom = g_object_get_data(G_OBJECT(tab), "custom-title");
        if (custom && g_strcmp0(custom, "Passwords") == 0) {
            browser_window_set_tab(self, i);
            return;
        }
    }

    BrowserTab *tab = browser_tab_new("about:passwords", FALSE);
    g_object_set_data_full(G_OBJECT(tab), "custom-title", g_strdup("Passwords"), g_free);
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    PasswordManager *pm = password_manager_get_default();
    GtkWidget *passwords_page = password_manager_create_full_page(pm);

    char *name = g_strdup_printf("tab-id-%u", self->tab_id_counter++);
    gtk_stack_add_named(GTK_STACK(self->tab_stack), passwords_page, name);
    g_object_set_data_full(G_OBJECT(tab), "stack-name", name, g_free);
    g_object_set_data(G_OBJECT(tab), "settings-page", passwords_page);

    int index = self->tabs->len - 1;
    self->current_tab = index;
    gtk_stack_set_visible_child(GTK_STACK(self->tab_stack), passwords_page);
    gtk_editable_set_text(GTK_EDITABLE(self->url_entry), "about:passwords");
    gtk_window_set_title(GTK_WINDOW(self), "Passwords - OpenBrowser");
    update_tab_bar(self);
}

/* Actions setup */
static const GActionEntry win_actions[] = {
    { "new-tab", on_new_tab_action, NULL, NULL, NULL },
    { "new-private-tab", on_new_private_tab_action, NULL, NULL, NULL },
    { "close-tab", on_close_tab_action, NULL, NULL, NULL },
    { "find", on_find_action, NULL, NULL, NULL },
    { "zoom-in", on_zoom_in_action, NULL, NULL, NULL },
    { "zoom-out", on_zoom_out_action, NULL, NULL, NULL },
    { "zoom-reset", on_zoom_reset_action, NULL, NULL, NULL },
    { "fullscreen", on_fullscreen_action, NULL, NULL, NULL },
    { "sidebar", on_sidebar_action, NULL, NULL, NULL },
    { "bookmark", on_bookmark_action, NULL, NULL, NULL },
    { "downloads", on_downloads_action, NULL, NULL, NULL },
    { "history", on_history_action, NULL, NULL, NULL },
    { "show-bookmarks", on_bookmarks_action, NULL, NULL, NULL },
    { "print", on_print_action, NULL, NULL, NULL },
    { "devtools", on_devtools_action, NULL, NULL, NULL },
    { "settings", on_settings_action, NULL, NULL, NULL },
    { "passwords", on_passwords_action, NULL, NULL, NULL },
    { "about", on_about_action, NULL, NULL, NULL },
    { "reload", on_reload_action, NULL, NULL, NULL },
    { "focus-url", on_focus_url_action, NULL, NULL, NULL },
    { "next-tab", on_next_tab_action, NULL, NULL, NULL },
    { "prev-tab", on_prev_tab_action, NULL, NULL, NULL },
};

static void
browser_window_dispose(GObject *object)
{
    BrowserWindow *self = BROWSER_WINDOW(object);
    g_clear_pointer(&self->tabs, g_ptr_array_unref);
    G_OBJECT_CLASS(browser_window_parent_class)->dispose(object);
}

static void
browser_window_class_init(BrowserWindowClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = browser_window_dispose;
}

static gboolean
on_scroll_zoom(GtkEventControllerScroll *controller, double dx, double dy, gpointer user_data)
{
    (void)controller; (void)dx;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* Only zoom when Ctrl is held */
    GdkModifierType state = gtk_event_controller_get_current_event_state(
        GTK_EVENT_CONTROLLER(controller));
    if (!(state & GDK_CONTROL_MASK))
        return FALSE;

    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return FALSE;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    double level = browser_tab_get_zoom_level(tab);

    if (dy < 0) {
        browser_tab_set_zoom_level(tab, level + 0.1);
    } else if (dy > 0) {
        browser_tab_set_zoom_level(tab, level - 0.1);
    }

    return TRUE;
}

static void
browser_window_init(BrowserWindow *self)
{
    self->tabs = g_ptr_array_new_with_free_func(g_object_unref);
    self->current_tab = -1;
    self->is_fullscreen = FALSE;
    self->tab_id_counter = 0;

    /* Window properties */
    gtk_window_set_title(GTK_WINDOW(self), "OpenBrowser");
    gtk_window_set_default_size(GTK_WINDOW(self), 1280, 800);
    gtk_widget_add_css_class(GTK_WIDGET(self), "main-window");

    /* Actions */
    g_action_map_add_action_entries(G_ACTION_MAP(self), win_actions,
        G_N_ELEMENTS(win_actions), self);

    /* Main layout */
    GtkWidget *main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* ===== ROW 1: HEADER BAR (window controls only) ===== */
    self->header_bar = adw_header_bar_new();
    adw_header_bar_set_show_title(ADW_HEADER_BAR(self->header_bar), FALSE);
    gtk_widget_add_css_class(self->header_bar, "browser-headerbar");
    gtk_box_append(GTK_BOX(main_vbox), self->header_bar);

    /* ===== ROW 2: URL / NAV BAR ===== */
    GtkWidget *nav_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_widget_add_css_class(nav_bar, "nav-toolbar");

    /* Panel toggle button */
    GtkWidget *panel_toggle = gtk_button_new_from_icon_name("view-dual-symbolic");
    gtk_widget_add_css_class(panel_toggle, "flat");
    gtk_widget_add_css_class(panel_toggle, "compact-btn");
    gtk_widget_set_tooltip_text(panel_toggle, "Toggle Tab Panel");
    g_signal_connect(panel_toggle, "clicked", G_CALLBACK(on_toggle_panel_clicked), self);
    gtk_box_append(GTK_BOX(nav_bar), panel_toggle);

    self->back_btn = gtk_button_new_from_icon_name("go-previous-symbolic");
    gtk_widget_add_css_class(self->back_btn, "flat");
    gtk_widget_add_css_class(self->back_btn, "compact-btn");
    g_signal_connect(self->back_btn, "clicked", G_CALLBACK(on_back_clicked), self);
    gtk_box_append(GTK_BOX(nav_bar), self->back_btn);

    self->forward_btn = gtk_button_new_from_icon_name("go-next-symbolic");
    gtk_widget_add_css_class(self->forward_btn, "flat");
    gtk_widget_add_css_class(self->forward_btn, "compact-btn");
    g_signal_connect(self->forward_btn, "clicked", G_CALLBACK(on_forward_clicked), self);
    gtk_box_append(GTK_BOX(nav_bar), self->forward_btn);

    self->reload_btn = gtk_button_new_from_icon_name("view-refresh-symbolic");
    gtk_widget_add_css_class(self->reload_btn, "flat");
    gtk_widget_add_css_class(self->reload_btn, "compact-btn");
    g_signal_connect(self->reload_btn, "clicked", G_CALLBACK(on_reload_clicked), self);
    gtk_box_append(GTK_BOX(nav_bar), self->reload_btn);

    self->home_btn = gtk_button_new_from_icon_name("go-home-symbolic");
    gtk_widget_add_css_class(self->home_btn, "flat");
    gtk_widget_add_css_class(self->home_btn, "compact-btn");
    g_signal_connect(self->home_btn, "clicked", G_CALLBACK(on_home_clicked), self);
    gtk_box_append(GTK_BOX(nav_bar), self->home_btn);

    self->url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(self->url_entry), "Search or type a URL");
    gtk_widget_add_css_class(self->url_entry, "url-entry");
    gtk_widget_set_hexpand(self->url_entry, TRUE);
    g_signal_connect(self->url_entry, "activate", G_CALLBACK(on_url_entry_activate), self);
    gtk_box_append(GTK_BOX(nav_bar), self->url_entry);

    /* Go button */
    GtkWidget *go_btn = gtk_button_new_from_icon_name("go-next-symbolic");
    gtk_widget_add_css_class(go_btn, "flat");
    gtk_widget_add_css_class(go_btn, "compact-btn");
    gtk_widget_set_tooltip_text(go_btn, "Go");
    g_signal_connect(go_btn, "clicked", G_CALLBACK(on_go_clicked), self);
    gtk_box_append(GTK_BOX(nav_bar), go_btn);

    GtkWidget *bookmark_btn = gtk_button_new_from_icon_name("starred-symbolic");
    gtk_widget_add_css_class(bookmark_btn, "flat");
    gtk_widget_add_css_class(bookmark_btn, "compact-btn");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(bookmark_btn), "win.bookmark");
    gtk_box_append(GTK_BOX(nav_bar), bookmark_btn);

    GtkWidget *download_btn = gtk_button_new_from_icon_name("folder-download-symbolic");
    gtk_widget_add_css_class(download_btn, "flat");
    gtk_widget_add_css_class(download_btn, "compact-btn");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(download_btn), "win.downloads");
    gtk_box_append(GTK_BOX(nav_bar), download_btn);

    GtkWidget *menu_btn = create_menu_button(self);
    gtk_widget_add_css_class(menu_btn, "compact-btn");
    gtk_box_append(GTK_BOX(nav_bar), menu_btn);

    gtk_box_append(GTK_BOX(main_vbox), nav_bar);

    /* ===== PROGRESS BAR ===== */
    self->progress_bar = gtk_progress_bar_new();
    gtk_widget_add_css_class(self->progress_bar, "loading-bar");
    gtk_widget_set_visible(self->progress_bar, FALSE);
    gtk_box_append(GTK_BOX(main_vbox), self->progress_bar);

    /* ===== ROW 3: LEFT PANEL (tabs) + CONTENT ===== */
    GtkWidget *content_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(content_hbox, TRUE);
    gtk_widget_set_hexpand(content_hbox, TRUE);

    /* --- Left panel in a revealer --- */
    self->tab_panel_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(self->tab_panel_revealer),
        GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT);
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->tab_panel_revealer), TRUE);
    gtk_widget_set_hexpand(self->tab_panel_revealer, FALSE);

    GtkWidget *tab_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(tab_panel, "tab-panel");
    gtk_widget_set_size_request(tab_panel, 200, -1);
    gtk_widget_set_hexpand(tab_panel, FALSE);

    /* Inner box with rounded right corners */
    GtkWidget *tab_panel_inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(tab_panel_inner, "tab-panel-inner");
    gtk_widget_set_vexpand(tab_panel_inner, TRUE);

    /* Panel header */
    GtkWidget *panel_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(panel_header, "tab-panel-header");

    GtkWidget *panel_label = gtk_label_new("Tabs");
    gtk_widget_add_css_class(panel_label, "heading");
    gtk_label_set_xalign(GTK_LABEL(panel_label), 0);
    gtk_widget_set_hexpand(panel_label, TRUE);
    gtk_box_append(GTK_BOX(panel_header), panel_label);

    self->new_tab_btn = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_add_css_class(self->new_tab_btn, "flat");
    gtk_widget_add_css_class(self->new_tab_btn, "circular");
    gtk_widget_set_tooltip_text(self->new_tab_btn, "New Tab (Ctrl+T)");
    g_signal_connect(self->new_tab_btn, "clicked", G_CALLBACK(on_new_tab_clicked), self);
    gtk_box_append(GTK_BOX(panel_header), self->new_tab_btn);

    gtk_box_append(GTK_BOX(tab_panel_inner), panel_header);

    /* Tab list (scrollable) */
    GtkWidget *tab_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tab_scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(tab_scroll, TRUE);

    self->tab_bar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(self->tab_bar_box, 4);
    gtk_widget_set_margin_end(self->tab_bar_box, 4);
    gtk_widget_set_margin_top(self->tab_bar_box, 4);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(tab_scroll), self->tab_bar_box);
    gtk_box_append(GTK_BOX(tab_panel_inner), tab_scroll);

    gtk_box_append(GTK_BOX(tab_panel), tab_panel_inner);

    gtk_revealer_set_child(GTK_REVEALER(self->tab_panel_revealer), tab_panel);
    gtk_box_append(GTK_BOX(content_hbox), self->tab_panel_revealer);

    /* --- Right: Web content --- */
    GtkWidget *content_overlay = gtk_overlay_new();
    gtk_widget_set_hexpand(content_overlay, TRUE);
    gtk_widget_set_vexpand(content_overlay, TRUE);

    self->tab_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(self->tab_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(self->tab_stack), 80);
    gtk_widget_set_hexpand(self->tab_stack, TRUE);
    gtk_widget_set_vexpand(self->tab_stack, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(content_overlay), self->tab_stack);

    self->sidebar_revealer = create_sidebar(self);
    gtk_widget_set_halign(self->sidebar_revealer, GTK_ALIGN_START);
    gtk_widget_set_valign(self->sidebar_revealer, GTK_ALIGN_FILL);
    gtk_overlay_add_overlay(GTK_OVERLAY(content_overlay), self->sidebar_revealer);

    gtk_box_append(GTK_BOX(content_hbox), content_overlay);
    gtk_box_append(GTK_BOX(main_vbox), content_hbox);

    /* ===== DOWNLOAD NOTIFICATION BAR ===== */
    self->download_bar = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(self->download_bar),
        GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->download_bar), FALSE);

    GtkWidget *dl_bar_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(dl_bar_box, "download-notify-bar");

    /* Animated spinner */
    GtkWidget *dl_spinner = gtk_spinner_new();
    gtk_spinner_set_spinning(GTK_SPINNER(dl_spinner), TRUE);
    gtk_box_append(GTK_BOX(dl_bar_box), dl_spinner);

    /* Download icon */
    GtkWidget *dl_icon = gtk_image_new_from_icon_name("folder-download-symbolic");
    gtk_box_append(GTK_BOX(dl_bar_box), dl_icon);

    /* Label */
    self->download_bar_label = gtk_label_new("Downloading...");
    gtk_label_set_ellipsize(GTK_LABEL(self->download_bar_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(self->download_bar_label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(self->download_bar_label), 0);
    gtk_box_append(GTK_BOX(dl_bar_box), self->download_bar_label);

    /* Progress bar */
    self->download_bar_progress = gtk_progress_bar_new();
    gtk_widget_set_size_request(self->download_bar_progress, 150, -1);
    gtk_widget_set_valign(self->download_bar_progress, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(self->download_bar_progress, "download-progress");
    gtk_box_append(GTK_BOX(dl_bar_box), self->download_bar_progress);

    /* View Downloads button */
    GtkWidget *view_dl_btn = gtk_button_new_with_label("View");
    gtk_widget_add_css_class(view_dl_btn, "flat");
    gtk_widget_set_valign(view_dl_btn, GTK_ALIGN_CENTER);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(view_dl_btn), "win.downloads");
    gtk_box_append(GTK_BOX(dl_bar_box), view_dl_btn);

    gtk_revealer_set_child(GTK_REVEALER(self->download_bar), dl_bar_box);
    gtk_box_append(GTK_BOX(main_vbox), self->download_bar);
    self->download_pulse_id = 0;

    /* Wrap in toast overlay for notifications */
    GtkWidget *toast_overlay = adw_toast_overlay_new();
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(toast_overlay), main_vbox);
    g_object_set_data(G_OBJECT(self), "toast-overlay", toast_overlay);

    adw_application_window_set_content(ADW_APPLICATION_WINDOW(self), toast_overlay);

    /* Keyboard shortcuts */
    setup_shortcuts(self);

    /* Ctrl+Scroll to zoom */
    GtkEventController *scroll_ctrl = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll_ctrl, "scroll", G_CALLBACK(on_scroll_zoom), self);
    gtk_widget_add_controller(GTK_WIDGET(self), scroll_ctrl);

    /* Connect download signal to NetworkSession */
    /* Will connect after first tab is created - see browser_window_new */
}

BrowserWindow *
browser_window_new(AdwApplication *app)
{
    BrowserWindow *self = g_object_new(BROWSER_TYPE_WINDOW,
        "application", app,
        NULL);

    /* Open default tab with startup page */
    browser_window_new_tab(self, NULL);

    /* Connect download signal to the persistent NetworkSession */
    BrowserTab *first_tab = g_ptr_array_index(self->tabs, 0);
    WebKitWebView *wv = browser_tab_get_web_view(first_tab);
    WebKitNetworkSession *ns = webkit_web_view_get_network_session(wv);
    g_signal_connect(ns, "download-started", G_CALLBACK(on_download_started), self);

    return self;
}

void
browser_window_new_tab(BrowserWindow *self, const char *uri)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));

    BrowserTab *tab = browser_tab_new(uri, FALSE);
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    /* Use unique counter for stack child name */
    char *name = g_strdup_printf("tab-id-%u", self->tab_id_counter++);
    gtk_stack_add_named(GTK_STACK(self->tab_stack), GTK_WIDGET(tab), name);
    /* Store name on the widget for later lookup */
    g_object_set_data_full(G_OBJECT(tab), "stack-name", name, g_free);

    connect_tab_signals(self, tab);

    int index = self->tabs->len - 1;
    browser_window_set_tab(self, index);
}

void
browser_window_new_private_tab(BrowserWindow *self)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));

    BrowserTab *tab = browser_tab_new(NULL, TRUE);
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    char *name = g_strdup_printf("tab-id-%u", self->tab_id_counter++);
    gtk_stack_add_named(GTK_STACK(self->tab_stack), GTK_WIDGET(tab), name);
    g_object_set_data_full(G_OBJECT(tab), "stack-name", name, g_free);

    connect_tab_signals(self, tab);

    int index = self->tabs->len - 1;
    browser_window_set_tab(self, index);
}

void
browser_window_close_tab(BrowserWindow *self, int index)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    g_return_if_fail(index >= 0 && index < (int)self->tabs->len);

    /* Don't close the last tab, open a new one instead */
    if (self->tabs->len == 1) {
        browser_window_new_tab(self, NULL);
    }

    BrowserTab *tab = g_ptr_array_index(self->tabs, index);

    /* Check if it's a settings page (different widget in stack) */
    GtkWidget *settings_page = g_object_get_data(G_OBJECT(tab), "settings-page");
    if (settings_page) {
        gtk_stack_remove(GTK_STACK(self->tab_stack), settings_page);
    } else {
        gtk_stack_remove(GTK_STACK(self->tab_stack), GTK_WIDGET(tab));
    }

    g_ptr_array_remove_index(self->tabs, index);

    if (self->current_tab >= (int)self->tabs->len) {
        self->current_tab = self->tabs->len - 1;
    }

    browser_window_set_tab(self, self->current_tab);
}

void
browser_window_set_tab(BrowserWindow *self, int index)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    g_return_if_fail(index >= 0 && index < (int)self->tabs->len);

    self->current_tab = index;

    /* Use stored stack name to switch */
    BrowserTab *tab = g_ptr_array_index(self->tabs, index);

    /* Check if it's a settings page */
    GtkWidget *settings_page = g_object_get_data(G_OBJECT(tab), "settings-page");
    if (settings_page) {
        gtk_stack_set_visible_child(GTK_STACK(self->tab_stack), settings_page);
    } else {
        const char *stack_name = g_object_get_data(G_OBJECT(tab), "stack-name");
        if (stack_name) {
            gtk_stack_set_visible_child_name(GTK_STACK(self->tab_stack), stack_name);
        } else {
            gtk_stack_set_visible_child(GTK_STACK(self->tab_stack), GTK_WIDGET(tab));
        }
    }

    const char *uri = browser_tab_get_uri(tab);
    const char *custom_title = g_object_get_data(G_OBJECT(tab), "custom-title");
    const char *title = custom_title ? custom_title : browser_tab_get_title(tab);

    if (uri) {
        gtk_editable_set_text(GTK_EDITABLE(self->url_entry), uri);
    }

    if (title) {
        char *window_title = g_strdup_printf("%s - OpenBrowser", title);
        gtk_window_set_title(GTK_WINDOW(self), window_title);
        g_free(window_title);
    }

    update_navigation_buttons(self);
    update_tab_bar(self);
}

int
browser_window_get_current_tab(BrowserWindow *self)
{
    g_return_val_if_fail(BROWSER_IS_WINDOW(self), -1);
    return self->current_tab;
}

int
browser_window_get_tab_count(BrowserWindow *self)
{
    g_return_val_if_fail(BROWSER_IS_WINDOW(self), 0);
    return self->tabs->len;
}

void
browser_window_navigate(BrowserWindow *self, const char *uri)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    browser_tab_navigate(tab, uri);
}

void
browser_window_go_back(BrowserWindow *self)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    browser_tab_go_back(tab);
}

void
browser_window_go_forward(BrowserWindow *self)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    browser_tab_go_forward(tab);
}

void
browser_window_reload(BrowserWindow *self)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    if (browser_tab_is_loading(tab)) {
        browser_tab_stop_loading(tab);
    } else {
        browser_tab_reload(tab);
    }
}

void
browser_window_stop_loading(BrowserWindow *self)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    browser_tab_stop_loading(tab);
}

void
browser_window_toggle_fullscreen(BrowserWindow *self)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    self->is_fullscreen = !self->is_fullscreen;
    if (self->is_fullscreen) {
        gtk_window_fullscreen(GTK_WINDOW(self));
    } else {
        gtk_window_unfullscreen(GTK_WINDOW(self));
    }
}

void
browser_window_find_in_page(BrowserWindow *self, const char *text)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    browser_tab_find_text(tab, text);
}

void
browser_window_zoom_in(BrowserWindow *self)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    double level = browser_tab_get_zoom_level(tab);
    browser_tab_set_zoom_level(tab, level + 0.1);
}

void
browser_window_zoom_out(BrowserWindow *self)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    double level = browser_tab_get_zoom_level(tab);
    browser_tab_set_zoom_level(tab, level - 0.1);
}

void
browser_window_zoom_reset(BrowserWindow *self)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    browser_tab_set_zoom_level(tab, 1.0);
}

void
browser_window_toggle_sidebar(BrowserWindow *self)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    gboolean visible = gtk_revealer_get_reveal_child(GTK_REVEALER(self->sidebar_revealer));
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->sidebar_revealer), !visible);
}

void
browser_window_pin_tab(BrowserWindow *self, int index)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    g_return_if_fail(index >= 0 && index < (int)self->tabs->len);

    BrowserTab *tab = g_ptr_array_index(self->tabs, index);
    browser_tab_set_pinned(tab, !browser_tab_is_pinned(tab));
    update_tab_bar(self);
}

void
browser_window_duplicate_tab(BrowserWindow *self, int index)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    g_return_if_fail(index >= 0 && index < (int)self->tabs->len);

    BrowserTab *tab = g_ptr_array_index(self->tabs, index);
    const char *uri = browser_tab_get_uri(tab);
    browser_window_new_tab(self, uri);
}

void
browser_window_mute_tab(BrowserWindow *self, int index)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    g_return_if_fail(index >= 0 && index < (int)self->tabs->len);

    BrowserTab *tab = g_ptr_array_index(self->tabs, index);
    browser_tab_set_muted(tab, !browser_tab_is_muted(tab));
    update_tab_bar(self);
}
