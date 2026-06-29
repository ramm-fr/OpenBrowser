#include "browser-window.h"
#include "browser-tab.h"
#include "download-manager.h"
#include "bookmark-manager.h"
#include "history-manager.h"
#include "settings-manager.h"
#include "password-manager.h"
#include "extension-manager.h"
#include <string.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

/* Forward declarations */
static void open_html_page_tab(BrowserWindow *self, const char *resource_path, const char *title, const char *url);

struct _BrowserWindow {
    AdwApplicationWindow parent_instance;

    /* Header / Toolbar */
    GtkWidget *header_bar;
    GtkWidget *nav_bar;
    GtkWidget *url_entry;
    GtkWidget *security_btn;   /* Lock icon before URL bar */
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
    guint tab_bar_update_id; /* coalesced tab-bar refresh source id */

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

    /* Right menu panel */
    GtkWidget *menu_panel_revealer;
    GtkWidget *menu_scrim;   /* click-catcher to close menu when clicking outside */

    /* Zoom indicator */
    GtkWidget *zoom_indicator;   /* revealer overlay */
    GtkWidget *zoom_label;       /* "100%" */
    guint zoom_hide_id;

    /* Pinned extensions box in toolbar */
    GtkWidget *ext_pinned_box;

    /* State */
    gboolean is_fullscreen;
};

G_DEFINE_TYPE(BrowserWindow, browser_window, ADW_TYPE_APPLICATION_WINDOW)

/* Forward declarations */
static void update_navigation_buttons(BrowserWindow *self);
static void set_chrome_visible(BrowserWindow *self, gboolean visible);
static void update_chrome_for_uri(BrowserWindow *self, const char *uri);
static void update_security_icon(BrowserWindow *self);
static void show_zoom_indicator(BrowserWindow *self);
static void update_tab_bar(BrowserWindow *self);
static void schedule_tab_bar_update(BrowserWindow *self);
static void on_tab_title_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data);
static void on_tab_favicon_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data);
static void on_tab_uri_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data);
static void on_tab_load_changed(WebKitWebView *web_view, WebKitLoadEvent event, gpointer user_data);
static void on_tab_progress_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data);
static void connect_tab_signals(BrowserWindow *self, BrowserTab *tab);
static GtkWidget *on_web_view_create(WebKitWebView *web_view, WebKitNavigationAction *navigation_action, gpointer user_data);
static gboolean on_popup_webview_attach(gpointer user_data);
static void on_web_view_close(WebKitWebView *web_view, gpointer user_data);
static gboolean on_decide_policy(WebKitWebView *web_view, WebKitPolicyDecision *decision, WebKitPolicyDecisionType type, gpointer user_data);

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

        /* Load startup page via custom scheme */
        webkit_web_view_load_uri(web_view, "open://home");
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
    if (g_strcmp0(uri, "about:blank") == 0) return;
    if (g_strstr_len(uri, -1, "openbrowser.local")) return;

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

    /* If Downloads tab already exists, close it so we recreate with fresh content */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        const char *custom = g_object_get_data(G_OBJECT(tab), "custom-title");
        if (custom && g_strcmp0(custom, "Downloads") == 0) {
            browser_window_close_tab(self, i);
            break;
        }
    }

    /* Create a native downloads page using the download manager widget */
    DownloadManager *dm = download_manager_get_default();
    GtkWidget *downloads_page = download_manager_create_full_page(dm);

    /* Create tab and hide its web view, replace with native downloads widget */
    BrowserTab *tab = browser_tab_new("data:text/html,<html></html>", FALSE);
    g_object_set_data_full(G_OBJECT(tab), "custom-title", g_strdup("Downloads"), g_free);
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    /* Hide the web view and add our native widget to the tab box */
    WebKitWebView *wv = browser_tab_get_web_view(tab);
    gtk_widget_set_visible(GTK_WIDGET(wv), FALSE);
    gtk_box_append(GTK_BOX(tab), downloads_page);

    char *name = g_strdup_printf("tab-id-%u", self->tab_id_counter++);
    gtk_stack_add_named(GTK_STACK(self->tab_stack), GTK_WIDGET(tab), name);
    g_object_set_data_full(G_OBJECT(tab), "stack-name", name, g_free);

    int index = self->tabs->len - 1;
    self->current_tab = index;
    gtk_stack_set_visible_child(GTK_STACK(self->tab_stack), GTK_WIDGET(tab));
    gtk_editable_set_text(GTK_EDITABLE(self->url_entry), "about:downloads");
    gtk_window_set_title(GTK_WINDOW(self), "Downloads - OpenBrowser");
    update_tab_bar(self);
}

static void
on_history_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* If History tab already exists, close it so we recreate with fresh content */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        const char *custom = g_object_get_data(G_OBJECT(tab), "custom-title");
        if (custom && g_strcmp0(custom, "History") == 0) {
            browser_window_close_tab(self, i);
            break;
        }
    }

    /* Create a native history page */
    HistoryManager *hm = history_manager_get_default();
    GtkWidget *history_page = history_manager_create_full_page(hm);

    /* Create tab and hide its web view, replace with native history widget */
    BrowserTab *tab = browser_tab_new("data:text/html,<html></html>", FALSE);
    g_object_set_data_full(G_OBJECT(tab), "custom-title", g_strdup("History"), g_free);
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    WebKitWebView *wv = browser_tab_get_web_view(tab);
    gtk_widget_set_visible(GTK_WIDGET(wv), FALSE);
    gtk_box_append(GTK_BOX(tab), history_page);

    char *name = g_strdup_printf("tab-id-%u", self->tab_id_counter++);
    gtk_stack_add_named(GTK_STACK(self->tab_stack), GTK_WIDGET(tab), name);
    g_object_set_data_full(G_OBJECT(tab), "stack-name", name, g_free);

    int index = self->tabs->len - 1;
    self->current_tab = index;
    gtk_stack_set_visible_child(GTK_STACK(self->tab_stack), GTK_WIDGET(tab));
    gtk_editable_set_text(GTK_EDITABLE(self->url_entry), "about:history");
    gtk_window_set_title(GTK_WINDOW(self), "History - OpenBrowser");
    update_tab_bar(self);
}

static void
on_bookmarks_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* If Bookmarks tab already exists, close it so we recreate with fresh content */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        const char *custom = g_object_get_data(G_OBJECT(tab), "custom-title");
        if (custom && g_strcmp0(custom, "Bookmarks") == 0) {
            browser_window_close_tab(self, i);
            break;
        }
    }

    /* Create a native bookmarks page */
    BookmarkManager *bm = bookmark_manager_get_default();
    GtkWidget *bookmarks_page = bookmark_manager_create_full_page(bm);

    /* Create tab and hide its web view, replace with native bookmarks widget */
    BrowserTab *tab = browser_tab_new("data:text/html,<html></html>", FALSE);
    g_object_set_data_full(G_OBJECT(tab), "custom-title", g_strdup("Bookmarks"), g_free);
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    WebKitWebView *wv = browser_tab_get_web_view(tab);
    gtk_widget_set_visible(GTK_WIDGET(wv), FALSE);
    gtk_box_append(GTK_BOX(tab), bookmarks_page);

    char *name = g_strdup_printf("tab-id-%u", self->tab_id_counter++);
    gtk_stack_add_named(GTK_STACK(self->tab_stack), GTK_WIDGET(tab), name);
    g_object_set_data_full(G_OBJECT(tab), "stack-name", name, g_free);

    int index = self->tabs->len - 1;
    self->current_tab = index;
    gtk_stack_set_visible_child(GTK_STACK(self->tab_stack), GTK_WIDGET(tab));
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

/* Helper: Open an HTML resource page as a tab */
static void
open_html_page_tab(BrowserWindow *self, const char *resource_path, const char *title, const char *url)
{
    /* Check if tab already exists */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        const char *custom = g_object_get_data(G_OBJECT(tab), "custom-title");
        if (custom && g_strcmp0(custom, title) == 0) {
            browser_window_set_tab(self, i);
            return;
        }
    }

    BrowserTab *tab = browser_tab_new("data:text/html,", FALSE);
    g_object_set_data_full(G_OBJECT(tab), "custom-title", g_strdup(title), g_free);
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    /* Load HTML */
    WebKitWebView *wv = browser_tab_get_web_view(tab);
    GBytes *bytes = g_resources_lookup_data(resource_path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
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
    gtk_editable_set_text(GTK_EDITABLE(self->url_entry), url);
    char *win_title = g_strdup_printf("%s - OpenBrowser", title);
    gtk_window_set_title(GTK_WINDOW(self), win_title);
    g_free(win_title);
    update_tab_bar(self);
}

static void
on_settings_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* If Settings tab already exists, close it so we recreate with fresh content */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        const char *custom = g_object_get_data(G_OBJECT(tab), "custom-title");
        if (custom && g_strcmp0(custom, "Settings") == 0) {
            browser_window_close_tab(self, i);
            break;
        }
    }

    /* Create a native settings page */
    SettingsManager *sm = settings_manager_get_default();
    GtkWidget *settings_page = settings_manager_create_page(sm);

    /* Create tab and hide its web view, replace with native settings widget */
    BrowserTab *tab = browser_tab_new("data:text/html,<html></html>", FALSE);
    g_object_set_data_full(G_OBJECT(tab), "custom-title", g_strdup("Settings"), g_free);
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    WebKitWebView *wv = browser_tab_get_web_view(tab);
    gtk_widget_set_visible(GTK_WIDGET(wv), FALSE);
    gtk_box_append(GTK_BOX(tab), settings_page);

    char *name = g_strdup_printf("tab-id-%u", self->tab_id_counter++);
    gtk_stack_add_named(GTK_STACK(self->tab_stack), GTK_WIDGET(tab), name);
    g_object_set_data_full(G_OBJECT(tab), "stack-name", name, g_free);

    int index = self->tabs->len - 1;
    self->current_tab = index;
    gtk_stack_set_visible_child(GTK_STACK(self->tab_stack), GTK_WIDGET(tab));
    gtk_editable_set_text(GTK_EDITABLE(self->url_entry), "about:settings");
    gtk_window_set_title(GTK_WINDOW(self), "Settings - OpenBrowser");
    update_tab_bar(self);
}

static void
on_extensions_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* If Extensions tab already exists, close it so we recreate with fresh content */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        const char *custom = g_object_get_data(G_OBJECT(tab), "custom-title");
        if (custom && g_strcmp0(custom, "Extensions") == 0) {
            browser_window_close_tab(self, i);
            break;
        }
    }

    ExtensionManager *em = extension_manager_get_default();
    GtkWidget *ext_page = extension_manager_create_page(em);

    BrowserTab *tab = browser_tab_new("data:text/html,<html></html>", FALSE);
    g_object_set_data_full(G_OBJECT(tab), "custom-title", g_strdup("Extensions"), g_free);
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    WebKitWebView *wv = browser_tab_get_web_view(tab);
    gtk_widget_set_visible(GTK_WIDGET(wv), FALSE);
    gtk_box_append(GTK_BOX(tab), ext_page);

    char *name = g_strdup_printf("tab-id-%u", self->tab_id_counter++);
    gtk_stack_add_named(GTK_STACK(self->tab_stack), GTK_WIDGET(tab), name);
    g_object_set_data_full(G_OBJECT(tab), "stack-name", name, g_free);

    int index = self->tabs->len - 1;
    self->current_tab = index;
    gtk_stack_set_visible_child(GTK_STACK(self->tab_stack), GTK_WIDGET(tab));
    gtk_editable_set_text(GTK_EDITABLE(self->url_entry), "about:extensions");
    gtk_window_set_title(GTK_WINDOW(self), "Extensions - OpenBrowser");
    update_tab_bar(self);
}

/* Menu creation */
static void
on_menu_toggle_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    gboolean visible = gtk_revealer_get_reveal_child(GTK_REVEALER(self->menu_panel_revealer));
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->menu_panel_revealer), !visible);
    if (self->menu_scrim)
        gtk_widget_set_visible(self->menu_scrim, !visible);
}

/* Clicking the content area while the menu is open closes the menu. */
static void
on_menu_scrim_pressed(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
    (void)gesture; (void)n_press; (void)x; (void)y;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->menu_panel_revealer), FALSE);
    if (self->menu_scrim)
        gtk_widget_set_visible(self->menu_scrim, FALSE);
}

static GtkWidget *
create_menu_button(BrowserWindow *self)
{
    (void)self;
    GtkWidget *button = gtk_button_new_from_icon_name("open-menu-symbolic");
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_add_css_class(button, "compact-btn");
    g_signal_connect(button, "clicked", G_CALLBACK(on_menu_toggle_clicked), self);
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

    /* Ctrl+= - Zoom In (without shift) */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>equal"),
            gtk_named_action_new("win.zoom-in")
        ));

    /* Ctrl+KP_Add - Zoom In (numpad) */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>KP_Add"),
            gtk_named_action_new("win.zoom-in")
        ));

    /* Ctrl+Minus - Zoom Out */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>minus"),
            gtk_named_action_new("win.zoom-out")
        ));

    /* Ctrl+KP_Subtract - Zoom Out (numpad) */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control>KP_Subtract"),
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

    /* Ctrl+Shift+R - Hard Reload (bypass cache) */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Control><Shift>r"),
            gtk_named_action_new("win.hard-reload")
        ));

    /* Shift+F5 - Hard Reload (bypass cache) */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Shift>F5"),
            gtk_named_action_new("win.hard-reload")
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
            gtk_named_action_new("win.go-back")
        ));

    /* Alt+Right - Forward */
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(controller),
        gtk_shortcut_new(
            gtk_shortcut_trigger_parse_string("<Alt>Right"),
            gtk_named_action_new("win.go-forward")
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

        /* Auto-open viewable files (PDF, images, media, text) with default app */
        if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(download), "auto-open"))) {
            char *uri;
            if (g_str_has_prefix(dest, "file://")) uri = g_strdup(dest);
            else uri = g_filename_to_uri(dest, NULL, NULL);
            if (uri) { g_app_info_launch_default_for_uri(uri, NULL, NULL); g_free(uri); }
        }
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

    /* Carry over the auto-open flag (set in decide-policy for PDFs etc.) */
    gboolean ao = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(self), "auto-open-next"));
    g_object_set_data(G_OBJECT(download), "auto-open", GINT_TO_POINTER(ao));
    g_object_set_data(G_OBJECT(self), "auto-open-next", GINT_TO_POINTER(0));

    /* Show the download bar with animation */
    gtk_label_set_text(GTK_LABEL(self->download_bar_label), "Starting download...");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->download_bar_progress), 0.0);
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->download_bar), TRUE);

    /* Add to download manager */
    DownloadManager *dm = download_manager_get_default();
    download_manager_add_download(dm, download);
}

/* Policy decision handler */
static gboolean
on_decide_policy(WebKitWebView *web_view, WebKitPolicyDecision *decision,
    WebKitPolicyDecisionType type, gpointer user_data)
{
    (void)user_data;

    if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
        WebKitResponsePolicyDecision *response_decision = WEBKIT_RESPONSE_POLICY_DECISION(decision);

        /* If WebKit can't show the content, download it */
        if (!webkit_response_policy_decision_is_mime_type_supported(response_decision)) {
            /* Flag PDFs / viewable docs to auto-open after download */
            WebKitURIResponse *resp = webkit_response_policy_decision_get_response(response_decision);
            const char *mime = resp ? webkit_uri_response_get_mime_type(resp) : NULL;
            GtkWidget *win = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(web_view)));
            gboolean openable = mime && (
                g_strcmp0(mime, "application/pdf") == 0 ||
                g_str_has_prefix(mime, "text/") ||
                g_str_has_prefix(mime, "image/") ||
                g_str_has_prefix(mime, "audio/") ||
                g_str_has_prefix(mime, "video/"));
            if (win && BROWSER_IS_WINDOW(win))
                g_object_set_data(G_OBJECT(win), "auto-open-next", GINT_TO_POINTER(openable ? 1 : 0));
            webkit_policy_decision_download(decision);
            return TRUE;
        }
    }

    /* Return FALSE for everything else — let WebKit handle naturally.
     * NEW_WINDOW_ACTION will trigger on_web_view_create signal. */
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

/* Handle window.close() from JavaScript — closes the tab that contains this WebView */
static void
on_web_view_close(WebKitWebView *web_view, gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* Find the tab containing this web view */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        /* Check if this tab's box contains the web_view */
        GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(tab));
        while (child) {
            if (WEBKIT_IS_WEB_VIEW(child) && WEBKIT_WEB_VIEW(child) == web_view) {
                browser_window_close_tab(self, i);
                return;
            }
            child = gtk_widget_get_next_sibling(child);
        }
    }
}

/* Handle requests to create new web views (window.open, target=_blank) */
static GtkWidget *
on_web_view_create(WebKitWebView *web_view, WebKitNavigationAction *navigation_action, gpointer user_data)
{
    (void)navigation_action;
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* Create a related WebView — shares process, session, cookies, and window.opener.
     * CRITICAL: Do NOT parent (gtk_box_append) the WebView before returning it.
     * WebKitGTK 2.52 crashes if returned WebView is already in a container
     * (WindowFeatures assertion). We parent it in an idle callback AFTER return. */
    WebKitWebView *new_web_view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "related-view", web_view,
        NULL));

    WebKitSettings *settings = webkit_web_view_get_settings(new_web_view);
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, TRUE);
    webkit_settings_set_enable_webgl(settings, TRUE);
    webkit_settings_set_enable_media(settings, TRUE);

    /* Store window reference on the WebView for idle callback */
    g_object_set_data(G_OBJECT(new_web_view), "owner-window", self);
    g_object_ref(new_web_view);
    g_idle_add(on_popup_webview_attach, new_web_view);

    return GTK_WIDGET(new_web_view);
}

/* Idle callback: attach popup WebView to a new tab AFTER create signal returns */
static gboolean
on_popup_webview_attach(gpointer user_data)
{
    WebKitWebView *new_web_view = WEBKIT_WEB_VIEW(user_data);
    BrowserWindow *self = g_object_get_data(G_OBJECT(new_web_view), "owner-window");

    if (!self || !BROWSER_IS_WINDOW(self)) {
        g_object_unref(new_web_view);
        return G_SOURCE_REMOVE;
    }

    /* Create a tab wrapper and embed this WebView */
    BrowserTab *tab = g_object_new(BROWSER_TYPE_TAB, NULL);

    gtk_widget_set_vexpand(GTK_WIDGET(new_web_view), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(new_web_view), TRUE);
    gtk_box_append(GTK_BOX(tab), GTK_WIDGET(new_web_view));

    /* Add tab to window */
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    char *name = g_strdup_printf("tab-id-%u", self->tab_id_counter++);
    gtk_stack_add_named(GTK_STACK(self->tab_stack), GTK_WIDGET(tab), name);
    g_object_set_data_full(G_OBJECT(tab), "stack-name", name, g_free);

    /* Connect signals */
    g_signal_connect(new_web_view, "notify::title", G_CALLBACK(on_tab_title_changed), self);
    g_signal_connect(new_web_view, "notify::uri", G_CALLBACK(on_tab_uri_changed), self);
    g_signal_connect(new_web_view, "load-changed", G_CALLBACK(on_tab_load_changed), self);
    g_signal_connect(new_web_view, "notify::estimated-load-progress",
        G_CALLBACK(on_tab_progress_changed), self);
    g_signal_connect(new_web_view, "decide-policy", G_CALLBACK(on_decide_policy), self);
    g_signal_connect(new_web_view, "create", G_CALLBACK(on_web_view_create), self);
    g_signal_connect(new_web_view, "close", G_CALLBACK(on_web_view_close), self);

    /* Switch to the new tab */
    int index = self->tabs->len - 1;
    browser_window_set_tab(self, index);

    g_object_unref(new_web_view);
    return G_SOURCE_REMOVE;
}

/* Web view signals connection */
static void
connect_tab_signals(BrowserWindow *self, BrowserTab *tab)
{
    WebKitWebView *web_view = browser_tab_get_web_view(tab);

    g_signal_connect(web_view, "notify::title", G_CALLBACK(on_tab_title_changed), self);
    g_signal_connect(web_view, "notify::uri", G_CALLBACK(on_tab_uri_changed), self);
    g_signal_connect(web_view, "notify::favicon", G_CALLBACK(on_tab_favicon_changed), self);
    g_signal_connect(web_view, "load-changed", G_CALLBACK(on_tab_load_changed), self);
    g_signal_connect(web_view, "notify::estimated-load-progress",
        G_CALLBACK(on_tab_progress_changed), self);
    g_signal_connect(web_view, "decide-policy",
        G_CALLBACK(on_decide_policy), self);
    g_signal_connect(web_view, "context-menu",
        G_CALLBACK(on_context_menu), self);
    g_signal_connect(web_view, "create",
        G_CALLBACK(on_web_view_create), self);
    g_signal_connect(web_view, "close",
        G_CALLBACK(on_web_view_close), self);
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
            /* Only update if tab panel is visible to avoid unnecessary widget rebuilds */
            if (gtk_revealer_get_reveal_child(GTK_REVEALER(self->tab_panel_revealer))) {
                schedule_tab_bar_update(self);
            }
            break;
        }
    }
}

/* Refresh tab bar when a favicon finishes loading */
static void
on_tab_favicon_changed(WebKitWebView *web_view, GParamSpec *pspec, gpointer user_data)
{
    (void)web_view; (void)pspec;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    if (gtk_revealer_get_reveal_child(GTK_REVEALER(self->tab_panel_revealer)))
        schedule_tab_bar_update(self);
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
                    if (g_str_has_prefix(uri, "open:"))
                        gtk_editable_set_text(GTK_EDITABLE(self->url_entry), "");
                    else
                        gtk_editable_set_text(GTK_EDITABLE(self->url_entry), uri);
                    update_chrome_for_uri(self, uri);
                }
                update_navigation_buttons(self);
            }
            /* History is now recorded in on_tab_load_changed at LOAD_FINISHED
             * to avoid saving intermediate redirects */
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
                        gtk_widget_set_opacity(self->progress_bar, 1.0);
                        gtk_button_set_icon_name(GTK_BUTTON(self->reload_btn), "process-stop-symbolic");
                        break;
                    case WEBKIT_LOAD_COMMITTED:
                        break;
                    case WEBKIT_LOAD_FINISHED:
                        gtk_widget_set_opacity(self->progress_bar, 0);
                        gtk_button_set_icon_name(GTK_BUTTON(self->reload_btn), "view-refresh-symbolic");
                        update_navigation_buttons(self);
                        update_security_icon(self);
                        break;
                    default:
                        break;
                }
            }
            /* Record history only at LOAD_FINISHED — avoids saving intermediate redirects */
            if (event == WEBKIT_LOAD_FINISHED && !browser_tab_is_private(tab)) {
                const char *uri = webkit_web_view_get_uri(web_view);
                const char *title = webkit_web_view_get_title(web_view);
                if (uri && title && *title) {
                    HistoryManager *hm = history_manager_get_default();
                    history_manager_add(hm, title, uri);
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

/* Show/hide the browser chrome (header, toolbar, tab panel) — used for the
 * fullscreen welcome screen. */
static void
set_chrome_visible(BrowserWindow *self, gboolean visible)
{
    /* Keep the header bar (window controls) so the window stays closable;
     * only hide the navigation toolbar and the side tab panel. */
    if (self->nav_bar) gtk_widget_set_visible(self->nav_bar, visible);
    if (self->tab_panel_revealer)
        gtk_revealer_set_reveal_child(GTK_REVEALER(self->tab_panel_revealer), visible);
}

/* Hide chrome while the welcome page is the active tab, restore otherwise */
static void
update_chrome_for_uri(BrowserWindow *self, const char *uri)
{
    gboolean welcome = uri && g_strstr_len(uri, -1, "welcome");
    set_chrome_visible(self, !welcome);
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

    /* Recreate tab items as a horizontal row of compact chips */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);

        /* Check for custom title (Downloads, Bookmarks, Settings) */
        const char *custom_title = g_object_get_data(G_OBJECT(tab), "custom-title");
        const char *title = custom_title ? custom_title : browser_tab_get_title(tab);

        /* Tab chip: icon + title + close */
        GtkWidget *tab_widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        gtk_widget_add_css_class(tab_widget, "tab-item");
        gtk_widget_set_size_request(tab_widget, 130, -1);
        gtk_widget_set_hexpand(tab_widget, FALSE);
        gtk_widget_set_valign(tab_widget, GTK_ALIGN_CENTER);
        if ((int)i == self->current_tab) {
            gtk_widget_add_css_class(tab_widget, "tab-item-active");
        }

        /* Favicon: use the site's icon if available, else a placeholder */
        GtkWidget *icon = NULL;
        const char *custom = g_object_get_data(G_OBJECT(tab), "custom-title");
        if (!custom && !browser_tab_is_private(tab)) {
            WebKitWebView *wv = browser_tab_get_web_view(tab);
            GdkTexture *favicon = webkit_web_view_get_favicon(wv);
            if (favicon) {
                icon = gtk_image_new_from_paintable(GDK_PAINTABLE(favicon));
            }
        }
        if (!icon) {
            const char *icon_name = browser_tab_is_private(tab) ?
                "security-high-symbolic" : "document-open-symbolic";
            icon = gtk_image_new_from_icon_name(icon_name);
        }
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
        gtk_box_append(GTK_BOX(tab_widget), icon);

        /* Title (fixed width so the tab never grows with long titles) */
        GtkWidget *label = gtk_label_new(title);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_label_set_width_chars(GTK_LABEL(label), 14);
        gtk_label_set_max_width_chars(GTK_LABEL(label), 14);
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

    /* "New tab" (+) button right after the last tab */
    self->new_tab_btn = gtk_button_new_from_icon_name("list-add-symbolic");
    gtk_widget_add_css_class(self->new_tab_btn, "flat");
    gtk_widget_add_css_class(self->new_tab_btn, "circular");
    gtk_widget_add_css_class(self->new_tab_btn, "new-tab-btn");
    gtk_widget_set_valign(self->new_tab_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(self->new_tab_btn, "New Tab (Ctrl+T)");
    g_signal_connect(self->new_tab_btn, "clicked", G_CALLBACK(on_new_tab_clicked), self);
    gtk_box_append(GTK_BOX(self->tab_bar_box), self->new_tab_btn);
}

/* Coalesce bursts of tab-bar refreshes (title/favicon notifications fire
 * rapidly during page load) into a single rebuild on the next idle so the
 * main thread isn't blocked rebuilding the strip many times in a row. */
static gboolean
do_tab_bar_update(gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    self->tab_bar_update_id = 0;
    update_tab_bar(self);
    return G_SOURCE_REMOVE;
}

static void
schedule_tab_bar_update(BrowserWindow *self)
{
    if (self->tab_bar_update_id != 0)
        return; /* already pending */
    self->tab_bar_update_id = g_idle_add(do_tab_bar_update, self);
}

static void
on_reload_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    browser_window_reload(BROWSER_WINDOW(user_data));
}

static void
on_go_back_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    browser_window_go_back(BROWSER_WINDOW(user_data));
}

static void
on_go_forward_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    browser_window_go_forward(BROWSER_WINDOW(user_data));
}

static void
on_hard_reload_action(GSimpleAction *action, GVariant *param, gpointer user_data)
{
    (void)action; (void)param;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    if (self->current_tab >= 0 && self->current_tab < (int)self->tabs->len) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
        browser_tab_hard_reload(tab);
    }
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

    /* If Passwords tab already exists, close it so we recreate with fresh content */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        const char *custom = g_object_get_data(G_OBJECT(tab), "custom-title");
        if (custom && g_strcmp0(custom, "Passwords") == 0) {
            browser_window_close_tab(self, i);
            break;
        }
    }

    /* Create a native passwords page */
    PasswordManager *pm = password_manager_get_default();
    GtkWidget *passwords_page = password_manager_create_full_page(pm);

    /* Create tab and hide its web view, replace with native passwords widget */
    BrowserTab *tab = browser_tab_new("data:text/html,<html></html>", FALSE);
    g_object_set_data_full(G_OBJECT(tab), "custom-title", g_strdup("Passwords"), g_free);
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    WebKitWebView *wv = browser_tab_get_web_view(tab);
    gtk_widget_set_visible(GTK_WIDGET(wv), FALSE);
    gtk_box_append(GTK_BOX(tab), passwords_page);

    char *name = g_strdup_printf("tab-id-%u", self->tab_id_counter++);
    gtk_stack_add_named(GTK_STACK(self->tab_stack), GTK_WIDGET(tab), name);
    g_object_set_data_full(G_OBJECT(tab), "stack-name", name, g_free);

    int index = self->tabs->len - 1;
    self->current_tab = index;
    gtk_stack_set_visible_child(GTK_STACK(self->tab_stack), GTK_WIDGET(tab));
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
    { "extensions", on_extensions_action, NULL, NULL, NULL },
    { "passwords", on_passwords_action, NULL, NULL, NULL },
    { "about", on_about_action, NULL, NULL, NULL },
    { "reload", on_reload_action, NULL, NULL, NULL },
    { "hard-reload", on_hard_reload_action, NULL, NULL, NULL },
    { "go-back", on_go_back_action, NULL, NULL, NULL },
    { "go-forward", on_go_forward_action, NULL, NULL, NULL },
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

/* ===== SITE SECURITY POPOVER ===== */

/* Destroy popover when it closes so layers don't stack up */
static void
on_site_popover_closed(GtkPopover *popover, gpointer user_data)
{
    (void)user_data;
    gtk_widget_unparent(GTK_WIDGET(popover));
}

/* Extract a field (e.g. "CN", "O") from an RFC2253 DN string like "CN=foo,O=bar" */
static char *
dn_get_field(const char *dn, const char *field)
{
    if (!dn) return NULL;
    char *needle = g_strdup_printf("%s=", field);
    const char *p = strstr(dn, needle);
    g_free(needle);
    if (!p) return NULL;
    p += strlen(field) + 1;
    const char *end = p;
    while (*end && *end != ',') end++;
    return g_strndup(p, end - p);
}

/* Add a labeled field row inside a card: "label" on top, "value" below */
static void
cert_add_field(GtkWidget *card, const char *label, const char *value)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(row, "cert-field");

    GtkWidget *lbl = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0);
    gtk_widget_add_css_class(lbl, "cert-field-label");
    gtk_box_append(GTK_BOX(row), lbl);

    GtkWidget *val = gtk_label_new(value && *value ? value : "Not part of certificate");
    gtk_label_set_xalign(GTK_LABEL(val), 0);
    gtk_label_set_wrap(GTK_LABEL(val), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(val), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_selectable(GTK_LABEL(val), TRUE);
    gtk_widget_set_hexpand(val, TRUE);
    gtk_widget_add_css_class(val, "cert-field-value");
    if (!value || !*value) gtk_widget_add_css_class(val, "dim-label");
    gtk_box_append(GTK_BOX(row), val);

    gtk_box_append(GTK_BOX(card), row);
}

/* Create a card with a section title; returns the card box to add fields to.
 * The titled wrapper is appended to `parent`. */
static GtkWidget *
cert_add_card(GtkWidget *parent, const char *title, const char *icon_name)
{
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(header, 16);
    gtk_widget_set_margin_bottom(header, 6);

    if (icon_name) {
        GtkWidget *icon = gtk_image_new_from_icon_name(icon_name);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
        gtk_box_append(GTK_BOX(header), icon);
    }
    GtkWidget *lbl = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0);
    gtk_widget_add_css_class(lbl, "cert-section-title");
    gtk_box_append(GTK_BOX(header), lbl);
    gtk_box_append(GTK_BOX(parent), header);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(card, "cert-card");
    gtk_box_append(GTK_BOX(parent), card);
    return card;
}

/* Compute SHA-256 hex fingerprint from a GByteArray of DER data */
static char *
cert_sha256_hex(GByteArray *der)
{
    if (!der || der->len == 0) return NULL;
    GChecksum *cs = g_checksum_new(G_CHECKSUM_SHA256);
    g_checksum_update(cs, der->data, der->len);
    const char *hex = g_checksum_get_string(cs);
    char *result = g_strdup(hex);
    g_checksum_free(cs);
    return result;
}

/* Open the certificate viewer as a tab in the content area */
static void
on_show_certificate(GtkButton *button, gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* Close the popover */
    GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_POPOVER);
    if (pop) gtk_popover_popdown(GTK_POPOVER(pop));

    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *cur_tab = g_ptr_array_index(self->tabs, self->current_tab);
    WebKitWebView *web_view = browser_tab_get_web_view(cur_tab);

    GTlsCertificate *cert = NULL;
    GTlsCertificateFlags tls_errors = 0;
    if (!webkit_web_view_get_tls_info(web_view, &cert, &tls_errors) || !cert)
        return;

    const char *uri = webkit_web_view_get_uri(web_view);
    GUri *guri = uri ? g_uri_parse(uri, G_URI_FLAGS_NONE, NULL) : NULL;
    const char *host = guri ? g_uri_get_host(guri) : "site";

    /* If a certificate tab already exists, close it first */
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *t = g_ptr_array_index(self->tabs, i);
        const char *custom = g_object_get_data(G_OBJECT(t), "custom-title");
        if (custom && g_strcmp0(custom, "Certificate") == 0) {
            browser_window_close_tab(self, i);
            break;
        }
    }

    /* Build the certificate page content */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_add_css_class(scrolled, "cert-page");

    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(page, 40);
    gtk_widget_set_margin_end(page, 40);
    gtk_widget_set_margin_top(page, 32);
    gtk_widget_set_margin_bottom(page, 32);
    gtk_widget_set_halign(page, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(page, 640, -1);

    /* Header with shield icon */
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    GtkWidget *header_icon = gtk_image_new_from_icon_name(
        tls_errors == 0 ? "channel-secure-symbolic" : "dialog-warning-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(header_icon), 36);
    gtk_widget_set_valign(header_icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(header_box), header_icon);

    GtkWidget *header_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title = gtk_label_new("Certificate");
    gtk_widget_add_css_class(title, "title-1");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_append(GTK_BOX(header_text), title);

    GtkWidget *subtitle = gtk_label_new(host ? host : "site");
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0);
    gtk_box_append(GTK_BOX(header_text), subtitle);
    gtk_widget_set_hexpand(header_text, TRUE);
    gtk_box_append(GTK_BOX(header_box), header_text);
    gtk_box_append(GTK_BOX(page), header_box);

    /* Get subject/issuer names */
    char *subject = g_tls_certificate_get_subject_name(cert);
    char *issuer = g_tls_certificate_get_issuer_name(cert);

    /* --- Issued To card --- */
    GtkWidget *card1 = cert_add_card(page, "Issued To", "avatar-default-symbolic");
    char *s_cn = dn_get_field(subject, "CN");
    char *s_o  = dn_get_field(subject, "O");
    char *s_ou = dn_get_field(subject, "OU");
    cert_add_field(card1, "Common Name (CN)", s_cn);
    cert_add_field(card1, "Organisation (O)", s_o);
    cert_add_field(card1, "Organisational Unit (OU)", s_ou);
    g_free(s_cn); g_free(s_o); g_free(s_ou);

    /* --- Issued By card --- */
    GtkWidget *card2 = cert_add_card(page, "Issued By", "security-high-symbolic");
    char *i_cn = dn_get_field(issuer, "CN");
    char *i_o  = dn_get_field(issuer, "O");
    char *i_ou = dn_get_field(issuer, "OU");
    cert_add_field(card2, "Common Name (CN)", i_cn);
    cert_add_field(card2, "Organisation (O)", i_o);
    cert_add_field(card2, "Organisational Unit (OU)", i_ou);
    g_free(i_cn); g_free(i_o); g_free(i_ou);

    /* --- Validity Period card --- */
    GtkWidget *card3 = cert_add_card(page, "Validity Period", "x-office-calendar-symbolic");
    GDateTime *not_before = g_tls_certificate_get_not_valid_before(cert);
    GDateTime *not_after = g_tls_certificate_get_not_valid_after(cert);
    char *nb_str = not_before ? g_date_time_format(not_before, "%A, %d %B %Y at %H:%M:%S") : NULL;
    char *na_str = not_after ? g_date_time_format(not_after, "%A, %d %B %Y at %H:%M:%S") : NULL;
    cert_add_field(card3, "Issued On", nb_str);
    cert_add_field(card3, "Expires On", na_str);
    g_free(nb_str); g_free(na_str);
    if (not_before) g_date_time_unref(not_before);
    if (not_after) g_date_time_unref(not_after);

    /* --- SHA-256 Fingerprints card --- */
    GtkWidget *card4 = cert_add_card(page, "SHA-256 Fingerprints", "dialog-password-symbolic");
    GByteArray *der = NULL;
    g_object_get(cert, "certificate", &der, NULL);
    char *fp = cert_sha256_hex(der);
    cert_add_field(card4, "Certificate", fp);
    g_free(fp);
    if (der) g_byte_array_unref(der);

    g_free(subject);
    g_free(issuer);
    if (guri) g_uri_unref(guri);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), page);

    /* Create a tab that hosts this native widget (like Settings/Downloads) */
    BrowserTab *tab = browser_tab_new("data:text/html,<html></html>", FALSE);
    g_object_set_data_full(G_OBJECT(tab), "custom-title", g_strdup("Certificate"), g_free);
    g_ptr_array_add(self->tabs, g_object_ref_sink(tab));

    WebKitWebView *wv = browser_tab_get_web_view(tab);
    gtk_widget_set_visible(GTK_WIDGET(wv), FALSE);
    gtk_box_append(GTK_BOX(tab), scrolled);

    char *name = g_strdup_printf("tab-id-%u", self->tab_id_counter++);
    gtk_stack_add_named(GTK_STACK(self->tab_stack), GTK_WIDGET(tab), name);
    g_object_set_data_full(G_OBJECT(tab), "stack-name", name, g_free);

    int index = self->tabs->len - 1;
    self->current_tab = index;
    gtk_stack_set_visible_child(GTK_STACK(self->tab_stack), GTK_WIDGET(tab));
    gtk_editable_set_text(GTK_EDITABLE(self->url_entry), "about:certificate");
    gtk_window_set_title(GTK_WINDOW(self), "Certificate - OpenBrowser");
    update_tab_bar(self);
}

/* ===== COOKIES & SITE DATA DIALOG ===== */

typedef struct {
    BrowserWindow *self;
    WebKitWebsiteDataManager *data_mgr;
    GtkWidget *list_box;       /* container for data rows */
    GtkWidget *empty_label;    /* shown when no data */
    char *base_domain;         /* registrable domain to filter by */
} SiteDataCtx;

static void
site_data_ctx_free(gpointer data)
{
    SiteDataCtx *ctx = data;
    g_free(ctx->base_domain);
    g_free(ctx);
}

/* Get last two labels of a host: "store.steampowered.com" -> "steampowered.com" */
static char *
get_base_domain(const char *host)
{
    if (!host) return NULL;
    int dots = 0;
    const char *p = host + strlen(host) - 1;
    const char *second_last_dot = NULL;
    while (p >= host) {
        if (*p == '.') {
            dots++;
            if (dots == 2) { second_last_dot = p; break; }
        }
        p--;
    }
    if (second_last_dot)
        return g_strdup(second_last_dot + 1);
    return g_strdup(host);
}

/* Remove a single website data entry */
static void
on_site_data_remove_clicked(GtkButton *button, gpointer user_data)
{
    SiteDataCtx *ctx = user_data;
    WebKitWebsiteData *entry = g_object_get_data(G_OBJECT(button), "website-data");
    GtkWidget *row = g_object_get_data(G_OBJECT(button), "row-widget");
    if (!entry) return;

    GList *to_remove = g_list_append(NULL, webkit_website_data_ref(entry));
    webkit_website_data_manager_remove(ctx->data_mgr,
        WEBKIT_WEBSITE_DATA_COOKIES | WEBKIT_WEBSITE_DATA_LOCAL_STORAGE |
        WEBKIT_WEBSITE_DATA_SESSION_STORAGE | WEBKIT_WEBSITE_DATA_INDEXEDDB_DATABASES |
        WEBKIT_WEBSITE_DATA_DISK_CACHE | WEBKIT_WEBSITE_DATA_MEMORY_CACHE,
        to_remove, NULL, NULL, NULL);
    g_list_free_full(to_remove, (GDestroyNotify)webkit_website_data_unref);

    if (row) gtk_widget_set_visible(row, FALSE);
}

/* Add a data entry row to the list */
static void
add_site_data_row(SiteDataCtx *ctx, WebKitWebsiteData *entry)
{
    const char *name = webkit_website_data_get_name(entry);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(row, "history-row");

    GtkWidget *icon = gtk_image_new_from_icon_name("preferences-system-privacy-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), icon);

    GtkWidget *name_label = gtk_label_new(name ? name : "(unknown)");
    gtk_label_set_xalign(GTK_LABEL(name_label), 0);
    gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(name_label, TRUE);
    gtk_box_append(GTK_BOX(row), name_label);

    GtkWidget *del_btn = gtk_button_new_from_icon_name("user-trash-symbolic");
    gtk_widget_add_css_class(del_btn, "flat");
    gtk_widget_add_css_class(del_btn, "circular");
    gtk_widget_set_valign(del_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(del_btn, "Remove this data");
    g_object_set_data_full(G_OBJECT(del_btn), "website-data",
        webkit_website_data_ref(entry), (GDestroyNotify)webkit_website_data_unref);
    g_object_set_data(G_OBJECT(del_btn), "row-widget", row);
    g_signal_connect(del_btn, "clicked", G_CALLBACK(on_site_data_remove_clicked), ctx);
    gtk_box_append(GTK_BOX(row), del_btn);

    gtk_box_append(GTK_BOX(ctx->list_box), row);
}

/* Async callback when website data has been fetched */
static void
on_website_data_fetched(GObject *source, GAsyncResult *result, gpointer user_data)
{
    SiteDataCtx *ctx = user_data;
    GError *error = NULL;
    GList *data_list = webkit_website_data_manager_fetch_finish(
        WEBKIT_WEBSITE_DATA_MANAGER(source), result, &error);

    if (error) {
        g_warning("Failed to fetch site data: %s", error->message);
        g_error_free(error);
        return;
    }

    int shown = 0;
    for (GList *l = data_list; l; l = l->next) {
        WebKitWebsiteData *entry = l->data;
        const char *name = webkit_website_data_get_name(entry);
        if (!name) continue;

        /* Show entries matching the current site's base domain (incl. subdomains) */
        gboolean match = FALSE;
        if (ctx->base_domain && *ctx->base_domain) {
            if (g_strcmp0(name, ctx->base_domain) == 0)
                match = TRUE;
            else if (g_str_has_suffix(name, ctx->base_domain)) {
                /* ensure it's a subdomain boundary (.base) */
                size_t nlen = strlen(name), blen = strlen(ctx->base_domain);
                if (nlen > blen && name[nlen - blen - 1] == '.')
                    match = TRUE;
            }
        }

        if (match) {
            add_site_data_row(ctx, entry);
            shown++;
        }
    }

    if (shown == 0 && ctx->empty_label)
        gtk_widget_set_visible(ctx->empty_label, TRUE);

    g_list_free_full(data_list, (GDestroyNotify)webkit_website_data_unref);
}

/* Open the "Cookies and site data" dialog */
static void
on_show_site_data(GtkButton *button, gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);

    /* Close the popover */
    GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_POPOVER);
    if (pop) gtk_popover_popdown(GTK_POPOVER(pop));

    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    WebKitWebView *web_view = browser_tab_get_web_view(tab);
    const char *uri = webkit_web_view_get_uri(web_view);
    if (!uri) return;

    GUri *guri = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
    if (!guri) return;
    const char *host = g_uri_get_host(guri);

    WebKitNetworkSession *session = webkit_web_view_get_network_session(web_view);
    WebKitWebsiteDataManager *data_mgr = webkit_network_session_get_website_data_manager(session);

    /* Build dialog */
    GtkWidget *dialog = adw_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "On-device site data");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 540, 560);

    GtkWidget *toolbar_view = adw_toolbar_view_new();
    GtkWidget *header = adw_header_bar_new();
    GtkWidget *title_label = gtk_label_new("On-device site data");
    gtk_widget_add_css_class(title_label, "heading");
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), title_label);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(content, 24);
    gtk_widget_set_margin_end(content, 24);
    gtk_widget_set_margin_top(content, 16);
    gtk_widget_set_margin_bottom(content, 24);

    /* Intro text */
    GtkWidget *intro = gtk_label_new(
        "Sites store data on your device to remember your activity and preferences. "
        "You can remove this data below.");
    gtk_label_set_xalign(GTK_LABEL(intro), 0);
    gtk_label_set_wrap(GTK_LABEL(intro), TRUE);
    gtk_widget_add_css_class(intro, "dim-label");
    gtk_box_append(GTK_BOX(content), intro);

    /* Section header */
    char *sec_text = g_strdup_printf("Data stored for %s", host ? host : "this site");
    GtkWidget *sec = gtk_label_new(sec_text);
    gtk_label_set_xalign(GTK_LABEL(sec), 0);
    gtk_widget_add_css_class(sec, "heading");
    gtk_widget_set_margin_top(sec, 8);
    gtk_box_append(GTK_BOX(content), sec);
    g_free(sec_text);

    /* List container */
    GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_append(GTK_BOX(content), list_box);

    /* Empty placeholder (hidden until fetch confirms no data) */
    GtkWidget *empty_label = gtk_label_new("No stored data found for this site.");
    gtk_widget_add_css_class(empty_label, "dim-label");
    gtk_widget_set_margin_top(empty_label, 16);
    gtk_widget_set_visible(empty_label, FALSE);
    gtk_box_append(GTK_BOX(content), empty_label);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), content);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), scrolled);

    /* Bottom bar with "Clear all" and "Done" */
    GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(bottom, 12);
    gtk_widget_set_margin_end(bottom, 12);
    gtk_widget_set_margin_top(bottom, 8);
    gtk_widget_set_margin_bottom(bottom, 8);

    GtkWidget *done_btn = gtk_button_new_with_label("Done");
    gtk_widget_add_css_class(done_btn, "suggested-action");
    gtk_widget_set_hexpand(done_btn, TRUE);
    gtk_widget_set_halign(done_btn, GTK_ALIGN_END);
    g_signal_connect_swapped(done_btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
    gtk_box_append(GTK_BOX(bottom), done_btn);

    adw_toolbar_view_add_bottom_bar(ADW_TOOLBAR_VIEW(toolbar_view), bottom);

    adw_window_set_content(ADW_WINDOW(dialog), toolbar_view);

    /* Build context and start async fetch */
    SiteDataCtx *ctx = g_new0(SiteDataCtx, 1);
    ctx->self = self;
    ctx->data_mgr = data_mgr;
    ctx->list_box = list_box;
    ctx->empty_label = empty_label;
    ctx->base_domain = get_base_domain(host);

    /* Free context when the dialog is destroyed */
    g_object_set_data_full(G_OBJECT(dialog), "site-data-ctx", ctx, site_data_ctx_free);

    webkit_website_data_manager_fetch(data_mgr,
        WEBKIT_WEBSITE_DATA_COOKIES | WEBKIT_WEBSITE_DATA_LOCAL_STORAGE |
        WEBKIT_WEBSITE_DATA_SESSION_STORAGE | WEBKIT_WEBSITE_DATA_INDEXEDDB_DATABASES |
        WEBKIT_WEBSITE_DATA_DISK_CACHE | WEBKIT_WEBSITE_DATA_MEMORY_CACHE,
        NULL, on_website_data_fetched, ctx);

    g_uri_unref(guri);
    gtk_window_present(GTK_WINDOW(dialog));
}

static void
on_security_btn_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    WebKitWebView *web_view = browser_tab_get_web_view(tab);
    const char *uri = webkit_web_view_get_uri(web_view);

    if (!uri || g_str_has_prefix(uri, "about:") || g_str_has_prefix(uri, "open:")) return;

    /* Parse URI */
    GUri *guri = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
    if (!guri) return;
    const char *host = g_uri_get_host(guri);
    const char *scheme = g_uri_get_scheme(guri);
    gboolean is_https = (g_strcmp0(scheme, "https") == 0);

    /* Get TLS info */
    GTlsCertificate *cert = NULL;
    GTlsCertificateFlags tls_errors = 0;
    gboolean has_tls = webkit_web_view_get_tls_info(web_view, &cert, &tls_errors);

    /* Build popover content */
    GtkWidget *popover = gtk_popover_new();
    gtk_widget_add_css_class(popover, "site-info-popover");
    gtk_widget_set_parent(popover, self->security_btn);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover), TRUE);
    g_signal_connect(popover, "closed", G_CALLBACK(on_site_popover_closed), NULL);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_size_request(box, 230, -1);

    /* Header: hostname */
    GtkWidget *host_label = gtk_label_new(host ? host : "Unknown");
    gtk_widget_add_css_class(host_label, "heading");
    gtk_label_set_xalign(GTK_LABEL(host_label), 0);
    gtk_label_set_ellipsize(GTK_LABEL(host_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(box), host_label);

    /* Separator */
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* --- Connection secure row (clickable -> certificate viewer) --- */
    GtkWidget *conn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    const char *lock_icon = is_https ? "channel-secure-symbolic" : "channel-insecure-symbolic";
    GtkWidget *conn_icon = gtk_image_new_from_icon_name(lock_icon);
    gtk_image_set_pixel_size(GTK_IMAGE(conn_icon), 16);
    gtk_widget_set_valign(conn_icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(conn_row), conn_icon);

    const char *conn_status;
    if (!is_https) {
        conn_status = "Connection is not secure";
    } else if (has_tls && tls_errors != 0) {
        conn_status = "Certificate has issues";
    } else {
        conn_status = "Connection is secure";
    }

    GtkWidget *conn_label = gtk_label_new(conn_status);
    gtk_label_set_xalign(GTK_LABEL(conn_label), 0);
    gtk_widget_set_hexpand(conn_label, TRUE);
    gtk_box_append(GTK_BOX(conn_row), conn_label);

    /* Show arrow only when there's a certificate to view */
    if (is_https && has_tls) {
        GtkWidget *conn_arrow = gtk_image_new_from_icon_name("go-next-symbolic");
        gtk_image_set_pixel_size(GTK_IMAGE(conn_arrow), 14);
        gtk_widget_set_valign(conn_arrow, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(conn_row), conn_arrow);
    }

    if (is_https && has_tls) {
        /* Wrap in a button to open certificate viewer */
        GtkWidget *conn_btn = gtk_button_new();
        gtk_button_set_child(GTK_BUTTON(conn_btn), conn_row);
        gtk_widget_add_css_class(conn_btn, "site-info-row-btn");
        g_signal_connect(conn_btn, "clicked", G_CALLBACK(on_show_certificate), self);
        gtk_box_append(GTK_BOX(box), conn_btn);
    } else {
        gtk_widget_set_margin_top(conn_row, 2);
        gtk_widget_set_margin_bottom(conn_row, 2);
        gtk_box_append(GTK_BOX(box), conn_row);
    }

    /* Separator */
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* --- Cookies and site data section (clickable -> data dialog) --- */
    GtkWidget *cookie_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *cookie_icon = gtk_image_new_from_icon_name("preferences-system-privacy-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(cookie_icon), 16);
    gtk_widget_set_valign(cookie_icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(cookie_row), cookie_icon);

    GtkWidget *cookie_label = gtk_label_new("Cookies and site data");
    gtk_label_set_xalign(GTK_LABEL(cookie_label), 0);
    gtk_widget_set_hexpand(cookie_label, TRUE);
    gtk_box_append(GTK_BOX(cookie_row), cookie_label);

    GtkWidget *cookie_arrow = gtk_image_new_from_icon_name("go-next-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(cookie_arrow), 14);
    gtk_widget_set_valign(cookie_arrow, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(cookie_row), cookie_arrow);

    GtkWidget *cookie_btn = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(cookie_btn), cookie_row);
    gtk_widget_add_css_class(cookie_btn, "site-info-row-btn");
    g_signal_connect(cookie_btn, "clicked", G_CALLBACK(on_show_site_data), self);
    gtk_box_append(GTK_BOX(box), cookie_btn);

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    gtk_popover_popup(GTK_POPOVER(popover));

    g_uri_unref(guri);
}

/* Update the security icon in the URL bar based on current page */
static void
update_security_icon(BrowserWindow *self)
{
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    WebKitWebView *web_view = browser_tab_get_web_view(tab);
    const char *uri = webkit_web_view_get_uri(web_view);

    if (!uri || g_str_has_prefix(uri, "about:") || g_str_has_prefix(uri, "open:") ||
        g_str_has_prefix(uri, "data:")) {
        gtk_button_set_icon_name(GTK_BUTTON(self->security_btn), "content-loading-symbolic");
        gtk_widget_set_tooltip_text(self->security_btn, "Internal page");
        return;
    }

    gboolean is_https = g_str_has_prefix(uri, "https://");

    if (is_https) {
        GTlsCertificate *cert = NULL;
        GTlsCertificateFlags tls_errors = 0;
        webkit_web_view_get_tls_info(web_view, &cert, &tls_errors);

        if (tls_errors == 0) {
            gtk_button_set_icon_name(GTK_BUTTON(self->security_btn), "channel-secure-symbolic");
            gtk_widget_set_tooltip_text(self->security_btn, "Connection is secure");
        } else {
            gtk_button_set_icon_name(GTK_BUTTON(self->security_btn), "dialog-warning-symbolic");
            gtk_widget_set_tooltip_text(self->security_btn, "Certificate has issues");
        }
    } else {
        gtk_button_set_icon_name(GTK_BUTTON(self->security_btn), "channel-insecure-symbolic");
        gtk_widget_set_tooltip_text(self->security_btn, "Connection is not secure");
    }
}

/* ===== Copy session to clipboard (toolbar button) ===== */

/* JS that serializes cookies + localStorage into a base64 session token */
static const char *SESSION_EXTRACT_JS =
    "(function(){try{var ls={};for(var i=0;i<localStorage.length;i++){"
    "var k=localStorage.key(i);ls[k]=localStorage.getItem(k);}"
    "var d={h:location.hostname,c:document.cookie,l:ls};"
    "return btoa(unescape(encodeURIComponent(JSON.stringify(d))));}catch(e){return '';}})()";

static void
on_session_extracted(GObject *source, GAsyncResult *result, gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    GError *error = NULL;
    JSCValue *value = webkit_web_view_evaluate_javascript_finish(
        WEBKIT_WEB_VIEW(source), result, &error);

    GtkWidget *overlay = g_object_get_data(G_OBJECT(self), "toast-overlay");

    if (error) {
        g_error_free(error);
        if (overlay && ADW_IS_TOAST_OVERLAY(overlay))
            adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(overlay),
                adw_toast_new("Couldn't read session"));
        return;
    }

    char *token = value ? jsc_value_to_string(value) : NULL;

    if (token && *token) {
        GdkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(self));
        gdk_clipboard_set_text(clipboard, token);
        if (overlay && ADW_IS_TOAST_OVERLAY(overlay)) {
            AdwToast *t = adw_toast_new("Session copied to clipboard");
            adw_toast_set_timeout(t, 2);
            adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(overlay), t);
        }
    } else {
        if (overlay && ADW_IS_TOAST_OVERLAY(overlay))
            adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(overlay),
                adw_toast_new("No session data on this page"));
    }

    g_free(token);
    if (value) g_object_unref(value);
}

static void
on_copy_session_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    WebKitWebView *web_view = browser_tab_get_web_view(tab);
    const char *uri = webkit_web_view_get_uri(web_view);

    if (!uri || g_str_has_prefix(uri, "about:") || g_str_has_prefix(uri, "open:")) {
        GtkWidget *overlay = g_object_get_data(G_OBJECT(self), "toast-overlay");
        if (overlay && ADW_IS_TOAST_OVERLAY(overlay))
            adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(overlay),
                adw_toast_new("Open a website first"));
        return;
    }

    webkit_web_view_evaluate_javascript(web_view, SESSION_EXTRACT_JS, -1,
        NULL, NULL, NULL, on_session_extracted, self);
}

/* ===== Native cookie import (Cookie-Editor JSON format) ===== */

typedef struct { BrowserWindow *self; GtkWidget *textview; GtkWidget *dialog; } CookieImportCtx;

static int
import_cookies_json(BrowserWindow *self, const char *json_text)
{
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return -1;
    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    WebKitWebView *web_view = browser_tab_get_web_view(tab);
    WebKitNetworkSession *session = webkit_web_view_get_network_session(web_view);
    WebKitCookieManager *mgr = webkit_network_session_get_cookie_manager(session);

    JsonParser *parser = json_parser_new();
    if (!json_parser_load_from_data(parser, json_text, -1, NULL)) { g_object_unref(parser); return -1; }
    JsonNode *root = json_parser_get_root(parser);
    JsonArray *arr = NULL;
    if (root && JSON_NODE_HOLDS_ARRAY(root)) arr = json_node_get_array(root);
    else if (root && JSON_NODE_HOLDS_OBJECT(root)) {
        JsonObject *o = json_node_get_object(root);
        if (json_object_has_member(o, "cookies")) arr = json_object_get_array_member(o, "cookies");
    }
    if (!arr) { g_object_unref(parser); return -1; }

    int count = 0;
    for (guint i = 0; i < json_array_get_length(arr); i++) {
        JsonObject *c = json_array_get_object_element(arr, i);
        if (!c) continue;
        const char *name = json_object_get_string_member_with_default(c, "name", NULL);
        const char *value = json_object_get_string_member_with_default(c, "value", NULL);
        const char *domain = json_object_get_string_member_with_default(c, "domain", NULL);
        const char *path = json_object_get_string_member_with_default(c, "path", "/");
        if (!name || !value || !domain) continue;
        SoupCookie *cookie = soup_cookie_new(name, value, domain, path, -1);
        if (json_object_has_member(c, "secure"))
            soup_cookie_set_secure(cookie, json_object_get_boolean_member(c, "secure"));
        if (json_object_has_member(c, "httpOnly"))
            soup_cookie_set_http_only(cookie, json_object_get_boolean_member(c, "httpOnly"));
        const char *ss = json_object_get_string_member_with_default(c, "sameSite", NULL);
        if (ss) {
            if (g_ascii_strcasecmp(ss, "strict") == 0) soup_cookie_set_same_site_policy(cookie, SOUP_SAME_SITE_POLICY_STRICT);
            else if (g_ascii_strcasecmp(ss, "lax") == 0) soup_cookie_set_same_site_policy(cookie, SOUP_SAME_SITE_POLICY_LAX);
            else soup_cookie_set_same_site_policy(cookie, SOUP_SAME_SITE_POLICY_NONE);
        }
        if (json_object_has_member(c, "expirationDate") && !json_object_get_null_member(c, "expirationDate")) {
            double exp = json_object_get_double_member(c, "expirationDate");
            GDateTime *dt = g_date_time_new_from_unix_utc((gint64)exp);
            if (dt) { soup_cookie_set_expires(cookie, dt); g_date_time_unref(dt); }
        }
        webkit_cookie_manager_add_cookie(mgr, cookie, NULL, NULL, NULL);
        soup_cookie_free(cookie);
        count++;
    }
    g_object_unref(parser);
    return count;
}

static void
on_cookie_import_apply(GtkButton *button, gpointer user_data)
{
    (void)button;
    CookieImportCtx *ctx = user_data;
    BrowserWindow *self = ctx->self;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ctx->textview));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    char *text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
    int n = import_cookies_json(self, text);
    g_free(text);
    GtkWidget *overlay = g_object_get_data(G_OBJECT(self), "toast-overlay");
    if (n > 0) {
        if (self->current_tab >= 0 && self->current_tab < (int)self->tabs->len) {
            BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
            webkit_web_view_reload(browser_tab_get_web_view(tab));
        }
        if (overlay && ADW_IS_TOAST_OVERLAY(overlay)) {
            char *msg = g_strdup_printf("Imported %d cookies — reloading", n);
            adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(overlay), adw_toast_new(msg));
            g_free(msg);
        }
        gtk_window_destroy(GTK_WINDOW(ctx->dialog));
    } else if (overlay && ADW_IS_TOAST_OVERLAY(overlay)) {
        adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(overlay), adw_toast_new("Invalid cookie JSON"));
    }
}

static void
on_import_cookies_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    GtkWidget *dialog = adw_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Import Cookies");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 520, 480);

    GtkWidget *tv = adw_toolbar_view_new();
    GtkWidget *header = adw_header_bar_new();
    GtkWidget *tlabel = gtk_label_new("Import Cookies");
    gtk_widget_add_css_class(tlabel, "heading");
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), tlabel);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), header);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 18); gtk_widget_set_margin_end(box, 18);
    gtk_widget_set_margin_top(box, 14); gtk_widget_set_margin_bottom(box, 14);
    GtkWidget *desc = gtk_label_new(
        "Paste cookies in Cookie-Editor JSON format (an array of cookie objects). "
        "This imports HttpOnly and Secure cookies too, so real logins work.");
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_label_set_xalign(GTK_LABEL(desc), 0);
    gtk_widget_add_css_class(desc, "dim-label");
    gtk_box_append(GTK_BOX(box), desc);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    GtkWidget *textview = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(textview), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textview), GTK_WRAP_CHAR);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), textview);
    gtk_box_append(GTK_BOX(box), scroll);
    GtkWidget *apply = gtk_button_new_with_label("Import & reload");
    gtk_widget_add_css_class(apply, "suggested-action");
    CookieImportCtx *ctx = g_new0(CookieImportCtx, 1);
    ctx->self = self; ctx->textview = textview; ctx->dialog = dialog;
    g_object_set_data_full(G_OBJECT(dialog), "cookie-import-ctx", ctx, g_free);
    g_signal_connect(apply, "clicked", G_CALLBACK(on_cookie_import_apply), ctx);
    gtk_box_append(GTK_BOX(box), apply);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), box);
    adw_window_set_content(ADW_WINDOW(dialog), tv);
    gtk_window_present(GTK_WINDOW(dialog));
}

/* Session toolbar button → popover (copy / import) */
static void
on_session_btn_clicked(GtkButton *button, gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    GtkWidget *popover = gtk_popover_new();
    gtk_widget_set_parent(popover, GTK_WIDGET(button));
    g_signal_connect(popover, "closed", G_CALLBACK(on_site_popover_closed), NULL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 6); gtk_widget_set_margin_end(box, 6);
    gtk_widget_set_margin_top(box, 6); gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_size_request(box, 210, -1);
    GtkWidget *copy = gtk_button_new_with_label("Copy session");
    gtk_widget_add_css_class(copy, "flat");
    gtk_widget_set_halign(gtk_button_get_child(GTK_BUTTON(copy)), GTK_ALIGN_START);
    g_signal_connect(copy, "clicked", G_CALLBACK(on_copy_session_clicked), self);
    g_signal_connect_swapped(copy, "clicked", G_CALLBACK(gtk_popover_popdown), popover);
    gtk_box_append(GTK_BOX(box), copy);
    GtkWidget *imp = gtk_button_new_with_label("Import cookies (JSON)…");
    gtk_widget_add_css_class(imp, "flat");
    gtk_widget_set_halign(gtk_button_get_child(GTK_BUTTON(imp)), GTK_ALIGN_START);
    g_signal_connect(imp, "clicked", G_CALLBACK(on_import_cookies_clicked), self);
    g_signal_connect_swapped(imp, "clicked", G_CALLBACK(gtk_popover_popdown), popover);
    gtk_box_append(GTK_BOX(box), imp);
    gtk_popover_set_child(GTK_POPOVER(popover), box);
    gtk_popover_popup(GTK_POPOVER(popover));
}

/* ===== Extensions toolbar popover (pin/unpin/delete) ===== */

static void
on_pinned_ext_clicked(GtkButton *button, gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    const char *id = g_object_get_data(G_OBJECT(button), "ext-id");
    ExtensionManager *em = extension_manager_get_default();
    gboolean now = !extension_manager_is_enabled(em, id);
    extension_manager_set_enabled(em, id, now);
    browser_window_reapply_extensions(self);
}

static void
update_pinned_extensions(BrowserWindow *self)
{
    if (!self->ext_pinned_box) return;
    GtkWidget *c = gtk_widget_get_first_child(self->ext_pinned_box);
    while (c) { GtkWidget *n = gtk_widget_get_next_sibling(c); gtk_box_remove(GTK_BOX(self->ext_pinned_box), c); c = n; }
    ExtensionManager *em = extension_manager_get_default();
    for (GList *l = extension_manager_get_installed(em); l; l = l->next) {
        Extension *e = l->data;
        if (!e->pinned) continue;
        GtkWidget *b = gtk_button_new_from_icon_name(e->icon ? e->icon : "application-x-addon-symbolic");
        gtk_widget_add_css_class(b, "flat"); gtk_widget_add_css_class(b, "compact-btn");
        if (!e->enabled) gtk_widget_set_opacity(b, 0.4);
        gtk_widget_set_tooltip_text(b, e->name);
        g_object_set_data_full(G_OBJECT(b), "ext-id", g_strdup(e->id), g_free);
        g_signal_connect(b, "clicked", G_CALLBACK(on_pinned_ext_clicked), self);
        gtk_box_append(GTK_BOX(self->ext_pinned_box), b);
    }
}

typedef struct { BrowserWindow *self; char *id; } ExtRowCtx;
static void ext_row_ctx_free(gpointer d){ ExtRowCtx *c=d; g_free(c->id); g_free(c); }

static void
on_ext_pin_toggle(GtkButton *button, gpointer user_data)
{
    ExtRowCtx *ctx = user_data;
    ExtensionManager *em = extension_manager_get_default();
    gboolean now = !extension_manager_is_pinned(em, ctx->id);
    extension_manager_set_pinned(em, ctx->id, now);
    gtk_widget_set_opacity(GTK_WIDGET(button), now ? 1.0 : 0.5);
    update_pinned_extensions(ctx->self);
}

static void
on_ext_delete(GtkButton *button, gpointer user_data)
{
    ExtRowCtx *ctx = user_data;
    extension_manager_uninstall(extension_manager_get_default(), ctx->id);
    GtkWidget *row = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_BOX);
    if (row) gtk_widget_set_visible(row, FALSE);
    browser_window_reapply_extensions(ctx->self);
}

static void
on_ext_manage_clicked(GtkButton *button, gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_POPOVER);
    if (pop) gtk_popover_popdown(GTK_POPOVER(pop));
    gtk_widget_activate_action(GTK_WIDGET(self), "win.extensions", NULL);
}

static void
on_ext_toolbar_clicked(GtkButton *button, gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    GtkWidget *popover = gtk_popover_new();
    gtk_widget_set_parent(popover, GTK_WIDGET(button));
    g_signal_connect(popover, "closed", G_CALLBACK(on_site_popover_closed), NULL);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(box, 8); gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 8); gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_size_request(box, 280, -1);
    GtkWidget *title = gtk_label_new("Extensions");
    gtk_widget_add_css_class(title, "heading");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_append(GTK_BOX(box), title);

    ExtensionManager *em = extension_manager_get_default();
    GList *inst = extension_manager_get_installed(em);
    if (!inst) {
        GtkWidget *empty = gtk_label_new("No extensions installed");
        gtk_widget_add_css_class(empty, "dim-label");
        gtk_box_append(GTK_BOX(box), empty);
    } else {
        for (GList *l = inst; l; l = l->next) {
            Extension *e = l->data;
            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_widget_add_css_class(row, "ext-pop-row");
            GtkWidget *icon = gtk_image_new_from_icon_name(e->icon ? e->icon : "application-x-addon-symbolic");
            gtk_image_set_pixel_size(GTK_IMAGE(icon), 18);
            gtk_box_append(GTK_BOX(row), icon);
            GtkWidget *name = gtk_label_new(e->name);
            gtk_label_set_xalign(GTK_LABEL(name), 0);
            gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
            gtk_widget_set_hexpand(name, TRUE);
            gtk_box_append(GTK_BOX(row), name);
            ExtRowCtx *ctx = g_new0(ExtRowCtx, 1);
            ctx->self = self; ctx->id = g_strdup(e->id);
            GtkWidget *pin = gtk_button_new_from_icon_name("view-pin-symbolic");
            gtk_widget_add_css_class(pin, "flat"); gtk_widget_add_css_class(pin, "circular");
            gtk_widget_set_tooltip_text(pin, "Pin to toolbar");
            gtk_widget_set_opacity(pin, e->pinned ? 1.0 : 0.5);
            g_object_set_data_full(G_OBJECT(pin), "ext-row-ctx", ctx, ext_row_ctx_free);
            g_signal_connect(pin, "clicked", G_CALLBACK(on_ext_pin_toggle), ctx);
            gtk_box_append(GTK_BOX(row), pin);
            GtkWidget *del = gtk_button_new_from_icon_name("user-trash-symbolic");
            gtk_widget_add_css_class(del, "flat"); gtk_widget_add_css_class(del, "circular");
            gtk_widget_set_tooltip_text(del, "Remove");
            g_signal_connect(del, "clicked", G_CALLBACK(on_ext_delete), ctx);
            gtk_box_append(GTK_BOX(row), del);
            gtk_box_append(GTK_BOX(box), row);
        }
    }
    gtk_box_append(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    GtkWidget *manage = gtk_button_new_with_label("Manage extensions");
    gtk_widget_add_css_class(manage, "flat");
    g_signal_connect(manage, "clicked", G_CALLBACK(on_ext_manage_clicked), self);
    gtk_box_append(GTK_BOX(box), manage);
    gtk_popover_set_child(GTK_POPOVER(popover), box);
    gtk_popover_popup(GTK_POPOVER(popover));
}

static gboolean
zoom_indicator_hide_cb(gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    self->zoom_hide_id = 0;
    if (self->zoom_indicator)
        gtk_revealer_set_reveal_child(GTK_REVEALER(self->zoom_indicator), FALSE);
    return G_SOURCE_REMOVE;
}

/* Show/refresh the zoom indicator panel with the current zoom level */
static void
show_zoom_indicator(BrowserWindow *self)
{
    if (!self->zoom_indicator || self->current_tab < 0 ||
        self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    double level = browser_tab_get_zoom_level(tab);
    int pct = (int)(level * 100.0 + 0.5);

    char *txt = g_strdup_printf("%d%%", pct);
    gtk_label_set_text(GTK_LABEL(self->zoom_label), txt);
    g_free(txt);

    gtk_revealer_set_reveal_child(GTK_REVEALER(self->zoom_indicator), TRUE);

    /* Reset the auto-hide timer */
    if (self->zoom_hide_id > 0)
        g_source_remove(self->zoom_hide_id);
    self->zoom_hide_id = g_timeout_add_seconds(2, zoom_indicator_hide_cb, self);
}

static void
on_zoom_ind_minus(GtkButton *button, gpointer user_data)
{
    (void)button;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    browser_window_zoom_out(self);
}

static void
on_zoom_ind_plus(GtkButton *button, gpointer user_data)
{
    (void)button;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    browser_window_zoom_in(self);
}

static void
on_zoom_ind_reset(GtkButton *button, gpointer user_data)
{
    (void)button;
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    browser_window_zoom_reset(self);
}

/* Build the floating zoom indicator (a revealer with %, -, +, Reset) */
static GtkWidget *
create_zoom_indicator(BrowserWindow *self)
{
    GtkWidget *revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer),
        GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer), 150);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer), FALSE);
    gtk_widget_add_css_class(revealer, "zoom-revealer");
    gtk_widget_set_halign(revealer, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(revealer, GTK_ALIGN_START);
    gtk_widget_set_margin_top(revealer, 16);

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(bar, "zoom-indicator");

    self->zoom_label = gtk_label_new("100%");
    gtk_widget_add_css_class(self->zoom_label, "zoom-pct");
    gtk_widget_set_size_request(self->zoom_label, 48, -1);
    gtk_box_append(GTK_BOX(bar), self->zoom_label);

    GtkWidget *minus = gtk_button_new_from_icon_name("zoom-out-symbolic");
    gtk_widget_add_css_class(minus, "flat");
    gtk_widget_add_css_class(minus, "circular");
    g_signal_connect(minus, "clicked", G_CALLBACK(on_zoom_ind_minus), self);
    gtk_box_append(GTK_BOX(bar), minus);

    GtkWidget *plus = gtk_button_new_from_icon_name("zoom-in-symbolic");
    gtk_widget_add_css_class(plus, "flat");
    gtk_widget_add_css_class(plus, "circular");
    g_signal_connect(plus, "clicked", G_CALLBACK(on_zoom_ind_plus), self);
    gtk_box_append(GTK_BOX(bar), plus);

    GtkWidget *reset = gtk_button_new_with_label("Reset");
    gtk_widget_add_css_class(reset, "zoom-reset-btn");
    g_signal_connect(reset, "clicked", G_CALLBACK(on_zoom_ind_reset), self);
    gtk_box_append(GTK_BOX(bar), reset);

    gtk_revealer_set_child(GTK_REVEALER(revealer), bar);
    return revealer;
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

    show_zoom_indicator(self);
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
    gtk_widget_set_size_request(GTK_WIDGET(self), 360, 400);
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
    self->nav_bar = nav_bar;
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

    /* Security/lock button before URL bar */
    self->security_btn = gtk_button_new_from_icon_name("channel-secure-symbolic");
    gtk_widget_add_css_class(self->security_btn, "flat");
    gtk_widget_add_css_class(self->security_btn, "compact-btn");
    gtk_widget_set_tooltip_text(self->security_btn, "Site information");
    g_signal_connect(self->security_btn, "clicked", G_CALLBACK(on_security_btn_clicked), self);
    gtk_box_append(GTK_BOX(nav_bar), self->security_btn);

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

    /* Session button (cookie icon) — copy / import cookies */
    GtkWidget *session_btn = gtk_button_new_from_icon_name("cookie-symbolic");
    gtk_widget_add_css_class(session_btn, "flat");
    gtk_widget_add_css_class(session_btn, "compact-btn");
    gtk_widget_set_tooltip_text(session_btn, "Session: copy or import cookies");
    g_signal_connect(session_btn, "clicked", G_CALLBACK(on_session_btn_clicked), self);
    gtk_box_append(GTK_BOX(nav_bar), session_btn);

    /* Pinned extensions box */
    self->ext_pinned_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(nav_bar), self->ext_pinned_box);

    /* Extensions button — quick manage popover */
    GtkWidget *ext_btn = gtk_button_new_from_icon_name("application-x-addon-symbolic");
    gtk_widget_add_css_class(ext_btn, "flat");
    gtk_widget_add_css_class(ext_btn, "compact-btn");
    gtk_widget_set_tooltip_text(ext_btn, "Extensions");
    g_signal_connect(ext_btn, "clicked", G_CALLBACK(on_ext_toolbar_clicked), self);
    gtk_box_append(GTK_BOX(nav_bar), ext_btn);

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

    /* ===== PROGRESS BAR (fixed height, no layout shift) ===== */
    self->progress_bar = gtk_progress_bar_new();
    gtk_widget_add_css_class(self->progress_bar, "loading-bar");
    gtk_widget_set_opacity(self->progress_bar, 0);
    gtk_box_append(GTK_BOX(main_vbox), self->progress_bar);

    /* ===== ROW 3: LEFT PANEL (tabs) + CONTENT ===== */
    GtkWidget *content_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_vexpand(content_hbox, TRUE);
    gtk_widget_set_hexpand(content_hbox, TRUE);

    /* --- Top tab bar in a revealer (below the URL/search box) --- */
    self->tab_panel_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(self->tab_panel_revealer),
        GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(self->tab_panel_revealer), 200);
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->tab_panel_revealer), TRUE);
    gtk_widget_set_vexpand(self->tab_panel_revealer, FALSE);

    GtkWidget *tab_panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(tab_panel, "tab-panel");
    gtk_widget_set_hexpand(tab_panel, TRUE);
    gtk_widget_set_vexpand(tab_panel, FALSE);

    /* Inner bar holding the scrollable tabs + new-tab button */
    GtkWidget *tab_panel_inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(tab_panel_inner, "tab-panel-inner");
    gtk_widget_set_hexpand(tab_panel_inner, TRUE);

    /* Tab list (horizontally scrollable) */
    GtkWidget *tab_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tab_scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_set_hexpand(tab_scroll, TRUE);

    self->tab_bar_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(self->tab_bar_box, 6);
    gtk_widget_set_margin_end(self->tab_bar_box, 6);
    gtk_widget_set_margin_top(self->tab_bar_box, 4);
    gtk_widget_set_margin_bottom(self->tab_bar_box, 4);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(tab_scroll), self->tab_bar_box);
    gtk_box_append(GTK_BOX(tab_panel_inner), tab_scroll);

    /* The "New tab" (+) button is created inside update_tab_bar() so it can
     * sit immediately after the last tab and scroll together with them. */

    gtk_box_append(GTK_BOX(tab_panel), tab_panel_inner);

    gtk_revealer_set_child(GTK_REVEALER(self->tab_panel_revealer), tab_panel);
    /* Place the tab bar at the top of the content, directly under the
     * navigation/URL bar (nav_bar and progress_bar were already added). */
    gtk_box_append(GTK_BOX(main_vbox), self->tab_panel_revealer);

    /* --- Right: Web content --- */
    GtkWidget *content_overlay = gtk_overlay_new();
    gtk_widget_set_hexpand(content_overlay, TRUE);
    gtk_widget_set_vexpand(content_overlay, TRUE);

    self->tab_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(self->tab_stack), GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_stack_set_transition_duration(GTK_STACK(self->tab_stack), 0);
    gtk_widget_set_hexpand(self->tab_stack, TRUE);
    gtk_widget_set_vexpand(self->tab_stack, TRUE);
    gtk_widget_add_css_class(self->tab_stack, "content-area");
    gtk_overlay_set_child(GTK_OVERLAY(content_overlay), self->tab_stack);

    self->sidebar_revealer = create_sidebar(self);
    gtk_widget_set_halign(self->sidebar_revealer, GTK_ALIGN_START);
    gtk_widget_set_valign(self->sidebar_revealer, GTK_ALIGN_FILL);
    gtk_overlay_add_overlay(GTK_OVERLAY(content_overlay), self->sidebar_revealer);

    /* Floating zoom indicator overlay (top-center) */
    self->zoom_indicator = create_zoom_indicator(self);
    gtk_overlay_add_overlay(GTK_OVERLAY(content_overlay), self->zoom_indicator);

    gtk_box_append(GTK_BOX(content_hbox), content_overlay);

    /* ===== RIGHT MENU PANEL ===== */
    /* Transparent click-catcher behind the menu: when the menu is open,
     * clicking anywhere on the content closes it. Hidden by default. */
    self->menu_scrim = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(self->menu_scrim, GTK_ALIGN_FILL);
    gtk_widget_set_valign(self->menu_scrim, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(self->menu_scrim, TRUE);
    gtk_widget_set_vexpand(self->menu_scrim, TRUE);
    gtk_widget_set_visible(self->menu_scrim, FALSE);
    {
        GtkGesture *scrim_click = gtk_gesture_click_new();
        g_signal_connect(scrim_click, "pressed", G_CALLBACK(on_menu_scrim_pressed), self);
        gtk_widget_add_controller(self->menu_scrim, GTK_EVENT_CONTROLLER(scrim_click));
    }
    gtk_overlay_add_overlay(GTK_OVERLAY(content_overlay), self->menu_scrim);

    self->menu_panel_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(self->menu_panel_revealer),
        GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
    gtk_revealer_set_transition_duration(GTK_REVEALER(self->menu_panel_revealer), 200);
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->menu_panel_revealer), FALSE);
    gtk_widget_set_hexpand(self->menu_panel_revealer, FALSE);
    gtk_widget_set_vexpand(self->menu_panel_revealer, TRUE);
    /* Float on the right edge of the content as an overlay popup. */
    gtk_widget_set_halign(self->menu_panel_revealer, GTK_ALIGN_END);
    gtk_widget_set_valign(self->menu_panel_revealer, GTK_ALIGN_FILL);

    GtkWidget *menu_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(menu_panel, "menu-panel");
    gtk_widget_set_size_request(menu_panel, 200, -1);
    gtk_widget_set_vexpand(menu_panel, TRUE);

    /* Menu panel header */
    GtkWidget *mp_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(mp_header, 12);
    gtk_widget_set_margin_end(mp_header, 12);
    gtk_widget_set_margin_top(mp_header, 10);
    gtk_widget_set_margin_bottom(mp_header, 6);
    GtkWidget *mp_title = gtk_label_new("Menu");
    gtk_widget_add_css_class(mp_title, "heading");
    gtk_widget_set_hexpand(mp_title, TRUE);
    gtk_label_set_xalign(GTK_LABEL(mp_title), 0);
    gtk_box_append(GTK_BOX(mp_header), mp_title);
    GtkWidget *mp_close = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(mp_close, "flat");
    gtk_widget_add_css_class(mp_close, "circular");
    g_signal_connect(mp_close, "clicked", G_CALLBACK(on_menu_toggle_clicked), self);
    gtk_box_append(GTK_BOX(mp_header), mp_close);
    gtk_box_append(GTK_BOX(menu_panel), mp_header);

    /* Menu items */
    GtkWidget *mp_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(mp_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(mp_scroll, TRUE);

    GtkWidget *mp_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(mp_list, 8);
    gtk_widget_set_margin_end(mp_list, 8);
    gtk_widget_set_margin_top(mp_list, 4);

    /* Helper to create menu items */
    #define MENU_ITEM(label_text, action_name) { \
        GtkWidget *btn = gtk_button_new_with_label(label_text); \
        gtk_widget_add_css_class(btn, "flat"); \
        gtk_widget_set_halign(btn, GTK_ALIGN_FILL); \
        gtk_actionable_set_action_name(GTK_ACTIONABLE(btn), action_name); \
        gtk_box_append(GTK_BOX(mp_list), btn); \
    }

    /* Section: Tabs */
    GtkWidget *s1 = gtk_label_new("TABS");
    gtk_widget_add_css_class(s1, "dim-label");
    gtk_widget_add_css_class(s1, "caption");
    gtk_label_set_xalign(GTK_LABEL(s1), 0);
    gtk_widget_set_margin_top(s1, 8);
    gtk_widget_set_margin_bottom(s1, 4);
    gtk_box_append(GTK_BOX(mp_list), s1);

    MENU_ITEM("New Tab", "win.new-tab");
    MENU_ITEM("New Private Tab", "win.new-private-tab");
    MENU_ITEM("Close Tab", "win.close-tab");

    /* Section: View */
    GtkWidget *s2 = gtk_label_new("VIEW");
    gtk_widget_add_css_class(s2, "dim-label");
    gtk_widget_add_css_class(s2, "caption");
    gtk_label_set_xalign(GTK_LABEL(s2), 0);
    gtk_widget_set_margin_top(s2, 12);
    gtk_widget_set_margin_bottom(s2, 4);
    gtk_box_append(GTK_BOX(mp_list), s2);

    MENU_ITEM("Zoom In", "win.zoom-in");
    MENU_ITEM("Zoom Out", "win.zoom-out");
    MENU_ITEM("Reset Zoom", "win.zoom-reset");
    MENU_ITEM("Full Screen", "win.fullscreen");

    /* Section: Tools */
    GtkWidget *s3 = gtk_label_new("TOOLS");
    gtk_widget_add_css_class(s3, "dim-label");
    gtk_widget_add_css_class(s3, "caption");
    gtk_label_set_xalign(GTK_LABEL(s3), 0);
    gtk_widget_set_margin_top(s3, 12);
    gtk_widget_set_margin_bottom(s3, 4);
    gtk_box_append(GTK_BOX(mp_list), s3);

    MENU_ITEM("Find in Page", "win.find");
    MENU_ITEM("Print", "win.print");
    MENU_ITEM("Developer Tools", "win.devtools");

    /* Section: Pages */
    GtkWidget *s4 = gtk_label_new("PAGES");
    gtk_widget_add_css_class(s4, "dim-label");
    gtk_widget_add_css_class(s4, "caption");
    gtk_label_set_xalign(GTK_LABEL(s4), 0);
    gtk_widget_set_margin_top(s4, 12);
    gtk_widget_set_margin_bottom(s4, 4);
    gtk_box_append(GTK_BOX(mp_list), s4);

    MENU_ITEM("Downloads", "win.downloads");
    MENU_ITEM("Bookmarks", "win.show-bookmarks");
    MENU_ITEM("History", "win.history");
    MENU_ITEM("Passwords", "win.passwords");
    MENU_ITEM("Extensions", "win.extensions");
    MENU_ITEM("Settings", "win.settings");
    MENU_ITEM("About", "win.about");

    #undef MENU_ITEM

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(mp_scroll), mp_list);
    gtk_box_append(GTK_BOX(menu_panel), mp_scroll);

    gtk_revealer_set_child(GTK_REVEALER(self->menu_panel_revealer), menu_panel);
    /* Add as a floating overlay over the web content (right side) so it
     * pops up on top instead of shrinking the page. */
    gtk_overlay_add_overlay(GTK_OVERLAY(content_overlay), self->menu_panel_revealer);

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

    /* Ctrl+Scroll to zoom - use CAPTURE phase so we get events before the WebView */
    GtkEventController *scroll_ctrl = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    gtk_event_controller_set_propagation_phase(scroll_ctrl, GTK_PHASE_CAPTURE);
    g_signal_connect(scroll_ctrl, "scroll", G_CALLBACK(on_scroll_zoom), self);
    gtk_widget_add_controller(GTK_WIDGET(self), scroll_ctrl);

    /* Load pinned extension icons into the toolbar */
    update_pinned_extensions(self);

    /* Connect download signal to NetworkSession */
    /* Will connect after first tab is created - see browser_window_new */
}

BrowserWindow *
browser_window_new(AdwApplication *app)
{
    BrowserWindow *self = g_object_new(BROWSER_TYPE_WINDOW,
        "application", app,
        NULL);

    /* First run? Show the welcome screen once, then never again */
    char *marker = g_build_filename(g_get_user_data_dir(), "openbrowser", ".welcomed", NULL);
    if (!g_file_test(marker, G_FILE_TEST_EXISTS)) {
        char *dir = g_build_filename(g_get_user_data_dir(), "openbrowser", NULL);
        g_mkdir_with_parents(dir, 0755);
        g_file_set_contents(marker, "1", 1, NULL);
        g_free(dir);
        browser_window_new_tab(self, "open://welcome");
        /* Fullscreen welcome — hide toolbar and tab panel */
        set_chrome_visible(self, FALSE);
    } else {
        /* Open default tab with startup page */
        browser_window_new_tab(self, NULL);
    }
    g_free(marker);

    /* Connect download signal to the persistent NetworkSession */
    BrowserTab *first_tab = g_ptr_array_index(self->tabs, 0);
    WebKitWebView *wv = browser_tab_get_web_view(first_tab);
    WebKitNetworkSession *ns = webkit_web_view_get_network_session(wv);
    g_signal_connect(ns, "download-started", G_CALLBACK(on_download_started), self);

    return self;
}

static gboolean
focus_url_entry_idle(gpointer user_data)
{
    BrowserWindow *self = BROWSER_WINDOW(user_data);
    gtk_widget_grab_focus(self->url_entry);
    gtk_editable_select_region(GTK_EDITABLE(self->url_entry), 0, -1);
    return G_SOURCE_REMOVE;
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

    /* For a blank new tab (no target URL), focus the URL bar so the
     * user can immediately start typing a URL/search. The focus is
     * deferred to an idle callback so it runs after the WebView has
     * been mapped, otherwise the WebView would steal focus. */
    if (!uri || !*uri) {
        gtk_editable_set_text(GTK_EDITABLE(self->url_entry), "");
        g_idle_add(focus_url_entry_idle, self);
    }
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
        if (g_str_has_prefix(uri, "open:"))
            gtk_editable_set_text(GTK_EDITABLE(self->url_entry), "");
        else
            gtk_editable_set_text(GTK_EDITABLE(self->url_entry), uri);
    }

    if (title) {
        char *window_title = g_strdup_printf("%s - OpenBrowser", title);
        gtk_window_set_title(GTK_WINDOW(self), window_title);
        g_free(window_title);
    }

    update_navigation_buttons(self);
    update_security_icon(self);
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
    show_zoom_indicator(self);
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
    show_zoom_indicator(self);
}

void
browser_window_zoom_reset(BrowserWindow *self)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    if (self->current_tab < 0 || self->current_tab >= (int)self->tabs->len)
        return;

    BrowserTab *tab = g_ptr_array_index(self->tabs, self->current_tab);
    browser_tab_set_zoom_level(tab, 1.0);
    show_zoom_indicator(self);
}

void
browser_window_toggle_sidebar(BrowserWindow *self)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    gboolean visible = gtk_revealer_get_reveal_child(GTK_REVEALER(self->sidebar_revealer));
    gtk_revealer_set_reveal_child(GTK_REVEALER(self->sidebar_revealer), !visible);
}

/* Re-apply extensions to every open web tab in real time and reload them */
void
browser_window_reapply_extensions(BrowserWindow *self)
{
    g_return_if_fail(BROWSER_IS_WINDOW(self));
    for (guint i = 0; i < self->tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(self->tabs, i);
        /* Skip native pages (Settings, Downloads, Extensions, etc.) */
        if (g_object_get_data(G_OBJECT(tab), "custom-title"))
            continue;
        const char *uri = browser_tab_get_uri(tab);
        /* Skip internal/blank pages */
        if (!uri || g_str_has_prefix(uri, "about:") || g_str_has_prefix(uri, "data:") ||
            g_str_has_prefix(uri, "open:"))
            continue;
        browser_tab_reapply_extensions(tab);
    }
    /* Keep toolbar pinned icons in sync */
    update_pinned_extensions(self);
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
