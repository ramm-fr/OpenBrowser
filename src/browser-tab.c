#include "browser-tab.h"
#include "settings-manager.h"
#include "password-manager.h"
#include "extension-manager.h"
#include <string.h>
#include <json-glib/json-glib.h>

struct _BrowserTab {
    GtkBox parent_instance;

    WebKitWebView *web_view;
    GtkWidget *find_bar;
    GtkWidget *find_entry;
    WebKitFindController *find_controller;

    gboolean is_private;
    gboolean is_pinned;
    gboolean is_muted;
    double zoom_level;
};

G_DEFINE_TYPE(BrowserTab, browser_tab, GTK_TYPE_BOX)

static void
browser_tab_dispose(GObject *object)
{
    BrowserTab *self = BROWSER_TAB(object);
    (void)self;
    G_OBJECT_CLASS(browser_tab_parent_class)->dispose(object);
}

static void
browser_tab_class_init(BrowserTabClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = browser_tab_dispose;
}

static void
on_find_entry_changed(GtkEditable *editable, gpointer user_data)
{
    BrowserTab *self = BROWSER_TAB(user_data);
    const char *text = gtk_editable_get_text(editable);
    if (text && *text) {
        webkit_find_controller_search(self->find_controller, text,
            WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE | WEBKIT_FIND_OPTIONS_WRAP_AROUND, 100);
    } else {
        webkit_find_controller_search_finish(self->find_controller);
    }
}

static void
on_find_next_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    BrowserTab *self = BROWSER_TAB(user_data);
    webkit_find_controller_search_next(self->find_controller);
}

static void
on_find_prev_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    BrowserTab *self = BROWSER_TAB(user_data);
    webkit_find_controller_search_previous(self->find_controller);
}

static void
on_find_close_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    BrowserTab *self = BROWSER_TAB(user_data);
    browser_tab_find_close(self);
}

static GtkWidget *
create_find_bar(BrowserTab *self)
{
    GtkWidget *wrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(wrap, GTK_ALIGN_END);
    gtk_widget_set_valign(wrap, GTK_ALIGN_START);
    gtk_widget_set_visible(wrap, FALSE);

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(bar, "find-bar");
    gtk_widget_set_halign(bar, GTK_ALIGN_END);

    self->find_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(self->find_entry), "Find…");
    gtk_widget_add_css_class(self->find_entry, "find-entry");
    gtk_widget_set_size_request(self->find_entry, 220, -1);
    gtk_box_append(GTK_BOX(bar), self->find_entry);

    g_signal_connect(self->find_entry, "changed", G_CALLBACK(on_find_entry_changed), self);

    GtkWidget *prev_btn = gtk_button_new_from_icon_name("go-up-symbolic");
    gtk_widget_add_css_class(prev_btn, "nav-button");
    gtk_widget_add_css_class(prev_btn, "circular");
    gtk_box_append(GTK_BOX(bar), prev_btn);
    g_signal_connect(prev_btn, "clicked", G_CALLBACK(on_find_prev_clicked), self);

    GtkWidget *next_btn = gtk_button_new_from_icon_name("go-down-symbolic");
    gtk_widget_add_css_class(next_btn, "nav-button");
    gtk_widget_add_css_class(next_btn, "circular");
    gtk_box_append(GTK_BOX(bar), next_btn);
    g_signal_connect(next_btn, "clicked", G_CALLBACK(on_find_next_clicked), self);

    GtkWidget *close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(close_btn, "nav-button");
    gtk_widget_add_css_class(close_btn, "circular");
    gtk_box_append(GTK_BOX(bar), close_btn);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_find_close_clicked), self);

    gtk_box_append(GTK_BOX(wrap), bar);
    return wrap;
}

/* Navigate message from startup page JavaScript */
static void
on_navigate_message(WebKitUserContentManager *ucm, JSCValue *js_result, gpointer user_data)
{
    (void)ucm;
    BrowserTab *self = BROWSER_TAB(user_data);

    char *uri = jsc_value_to_string(js_result);
    if (uri && *uri) {
        /* Route through browser_tab_navigate so search engine settings are applied */
        browser_tab_navigate(self, uri);
    }
    g_free(uri);
}

/* ---- User favorites (persisted natively for the new-tab page) ---------- */

static char *
favorites_file_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "openbrowser", "favorites.json", NULL);
}

/* Returns a JSON array string (caller frees). Defaults to "[]". */
static char *
read_favorites_json(void)
{
    char *path = favorites_file_path();
    char *contents = NULL;
    gsize len = 0;
    if (g_file_get_contents(path, &contents, &len, NULL) && contents && *contents) {
        g_free(path);
        return contents;
    }
    g_free(contents);
    g_free(path);
    return g_strdup("[]");
}

/* favSave: the new-tab page sends the full favorites array as JSON to save. */
static void
on_favorites_save_message(WebKitUserContentManager *ucm, JSCValue *js_result, gpointer user_data)
{
    (void)ucm; (void)user_data;
    char *json = jsc_value_to_string(js_result);
    if (!json) return;

    /* Validate it actually parses as JSON before writing. */
    JsonParser *parser = json_parser_new();
    if (json_parser_load_from_data(parser, json, -1, NULL)) {
        char *path = favorites_file_path();
        char *dir = g_path_get_dirname(path);
        g_mkdir_with_parents(dir, 0755);
        g_free(dir);
        g_file_set_contents(path, json, -1, NULL);
        g_free(path);
    }
    g_object_unref(parser);
    g_free(json);
}

/* ---- New-tab page preferences (Customise dialog) ----------------------- */

static char *
newtab_prefs_file_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "openbrowser", "newtab.json", NULL);
}

static char *
read_newtab_prefs_json(void)
{
    char *path = newtab_prefs_file_path();
    char *contents = NULL;
    gsize len = 0;
    if (g_file_get_contents(path, &contents, &len, NULL) && contents && *contents) {
        g_free(path);
        return contents;
    }
    g_free(contents);
    g_free(path);
    return g_strdup("{}");
}

/* prefsSave: the new-tab page sends its preferences object as JSON to save. */
static void
on_newtab_prefs_save_message(WebKitUserContentManager *ucm, JSCValue *js_result, gpointer user_data)
{
    (void)ucm; (void)user_data;
    char *json = jsc_value_to_string(js_result);
    if (!json) return;

    JsonParser *parser = json_parser_new();
    if (json_parser_load_from_data(parser, json, -1, NULL)) {
        char *path = newtab_prefs_file_path();
        char *dir = g_path_get_dirname(path);
        g_mkdir_with_parents(dir, 0755);
        g_free(dir);
        g_file_set_contents(path, json, -1, NULL);
        g_free(path);
    }
    g_object_unref(parser);
    g_free(json);
}

/* ---- New-tab content cache (news / weather / markets) -------------------
 * The new-tab page hits the network for these widgets. Re-fetching on every
 * new tab made the page slow and janky, so we cache the rendered data on disk
 * and push it back on load: a fresh tab renders instantly from cache and only
 * refreshes in the background when the cache is stale. */

static char *
newtab_cache_file_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "openbrowser", "newtab-cache.json", NULL);
}

static char *
read_newtab_cache_json(void)
{
    char *path = newtab_cache_file_path();
    char *contents = NULL;
    gsize len = 0;
    if (g_file_get_contents(path, &contents, &len, NULL) && contents && *contents) {
        g_free(path);
        return contents;
    }
    g_free(contents);
    g_free(path);
    return g_strdup("{}");
}

/* cacheSave: the new-tab page sends the latest widget data as JSON to cache. */
static void
on_newtab_cache_save_message(WebKitUserContentManager *ucm, JSCValue *js_result, gpointer user_data)
{
    (void)ucm; (void)user_data;
    char *json = jsc_value_to_string(js_result);
    if (!json) return;

    JsonParser *parser = json_parser_new();
    if (json_parser_load_from_data(parser, json, -1, NULL)) {
        char *path = newtab_cache_file_path();
        char *dir = g_path_get_dirname(path);
        g_mkdir_with_parents(dir, 0755);
        g_free(dir);
        g_file_set_contents(path, json, -1, NULL);
        g_free(path);
    }
    g_object_unref(parser);
    g_free(json);
}

/* When the new-tab page finishes loading, push the saved favorites into it. */
static void
on_startup_load_changed(WebKitWebView *web_view, WebKitLoadEvent event, gpointer user_data)
{
    (void)user_data;
    if (event != WEBKIT_LOAD_FINISHED) return;

    const char *uri = webkit_web_view_get_uri(web_view);
    if (!uri || !g_str_has_prefix(uri, "open://")) return;
    /* Only the home / new-tab page consumes favorites. */
    if (!g_strstr_len(uri, -1, "home")) return;

    char *fav = read_favorites_json();
    char *prefs = read_newtab_prefs_json();
    char *cache = read_newtab_cache_json();
    char *js = g_strdup_printf(
        "if(window.__obSetPrefs){window.__obSetPrefs(%s);}"
        "if(window.__obSetCache){window.__obSetCache(%s);}"
        "if(window.__obSetFavorites){window.__obSetFavorites(%s);}",
        prefs, cache, fav);
    webkit_web_view_evaluate_javascript(web_view, js, -1, NULL, NULL, NULL, NULL, NULL);
    g_free(js);
    g_free(prefs);
    g_free(cache);
    g_free(fav);
}

/* Password save message from JavaScript */
static void
on_password_save_message(WebKitUserContentManager *ucm, JSCValue *js_result, gpointer user_data)
{
    (void)ucm;
    (void)user_data;

    char *message = jsc_value_to_string(js_result);
    if (!message) return;

    /* Parse JSON: {"site":"...", "username":"...", "password":"..."} */
    JsonParser *parser = json_parser_new();
    if (json_parser_load_from_data(parser, message, -1, NULL)) {
        JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
        const char *site = json_object_get_string_member(obj, "site");
        const char *username = json_object_get_string_member(obj, "username");
        const char *password = json_object_get_string_member(obj, "password");

        if (site && username && password && *password) {
            PasswordManager *pm = password_manager_get_default();
            password_manager_add(pm, site, username, password);
            g_print("[OpenBrowser] Saved credentials for: %s (%s)\n", site, username);
        }
    }
    g_object_unref(parser);
    g_free(message);
}

/* Auto-fill saved credentials when page loads */
static void
on_page_load_for_autofill(WebKitWebView *web_view, WebKitLoadEvent event, gpointer user_data)
{
    (void)user_data;
    if (event != WEBKIT_LOAD_FINISHED) return;

    const char *uri = webkit_web_view_get_uri(web_view);
    if (!uri || g_str_has_prefix(uri, "about:")) return;

    /* Extract hostname */
    GUri *guri = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
    if (!guri) return;
    const char *host = g_uri_get_host(guri);
    if (!host) { g_uri_unref(guri); return; }

    PasswordManager *pm = password_manager_get_default();
    PasswordEntry *entry = password_manager_find(pm, host);

    if (entry && entry->username && entry->password) {
        /* Use JSON serialization for safe credential injection (prevents XSS) */
        JsonBuilder *builder = json_builder_new();
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "u");
        json_builder_add_string_value(builder, entry->username);
        json_builder_set_member_name(builder, "p");
        json_builder_add_string_value(builder, entry->password);
        json_builder_end_object(builder);

        JsonGenerator *gen = json_generator_new();
        JsonNode *root = json_builder_get_root(builder);
        json_generator_set_root(gen, root);
        char *json_str = json_generator_to_data(gen, NULL);

        char *js = g_strdup_printf(
            "(() => {"
            "  const creds = JSON.parse('%s');"
            "  const inputs = document.querySelectorAll('input');"
            "  let userField = null, passField = null;"
            "  inputs.forEach(inp => {"
            "    const type = (inp.type || '').toLowerCase();"
            "    const name = (inp.name || inp.id || '').toLowerCase();"
            "    if (type === 'password') passField = inp;"
            "    else if ((type === 'text' || type === 'email' || type === 'tel') &&"
            "      (name.includes('user') || name.includes('login') || name.includes('email')"
            "       || name.includes('name') || name.includes('account') || name.includes('phone')"
            "       || name.includes('id') || type === 'email' || type === 'tel')) {"
            "      userField = inp;"
            "    }"
            "  });"
            "  if (passField) {"
            "    if (userField) {"
            "      userField.value = creds.u;"
            "      userField.dispatchEvent(new Event('input', {bubbles:true}));"
            "    }"
            "    passField.value = creds.p;"
            "    passField.dispatchEvent(new Event('input', {bubbles:true}));"
            "  }"
            "})();",
            json_str);

        webkit_web_view_evaluate_javascript(web_view, js, -1, NULL, NULL, NULL, NULL, NULL);

        g_free(js);
        g_free(json_str);
        json_node_unref(root);
        g_object_unref(gen);
        g_object_unref(builder);
    }

    g_uri_unref(guri);
}

/* Load the styled error page (error.html) with the given type + url */
static void
load_error_page(WebKitWebView *web_view, const char *type, const char *failing_uri)
{
    GBytes *bytes = g_resources_lookup_data("/com/openbrowser/error.html",
        G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
    if (!bytes) return;
    gsize size;
    const char *data = g_bytes_get_data(bytes, &size);
    GString *s = g_string_new_len(data, size);
    g_string_replace(s, "__OB_ERROR__", type ? type : "generic", 0);
    g_string_replace(s, "__OB_URL__", failing_uri ? failing_uri : "", 0);
    char *tmp = g_string_free(s, FALSE);
    webkit_web_view_load_alternate_html(web_view, tmp, failing_uri, NULL);
    g_free(tmp);
    g_bytes_unref(bytes);
}

/* Load the offline game page */
static void
load_offline_page(WebKitWebView *web_view, const char *failing_uri)
{
    GBytes *bytes = g_resources_lookup_data("/com/openbrowser/offline.html",
        G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
    if (!bytes) return;
    gsize size;
    const char *data = g_bytes_get_data(bytes, &size);
    char *html = g_strndup(data, size);
    webkit_web_view_load_alternate_html(web_view, html, failing_uri, NULL);
    g_free(html);
    g_bytes_unref(bytes);
}

/* Handle load failures — offline game for connectivity loss, styled error page otherwise */
static gboolean
on_load_failed(WebKitWebView *web_view, WebKitLoadEvent load_event,
    const char *failing_uri, GError *error, gpointer user_data)
{
    (void)load_event;
    (void)user_data;

    if (g_error_matches(error, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED) ||
        g_error_matches(error, WEBKIT_POLICY_ERROR, WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE)) {
        return FALSE;
    }

    /* TLS/certificate problems → SSL error page */
    const char *m = (error && error->message) ? error->message : "";
    if (g_strstr_len(m, -1, "TLS") || g_strstr_len(m, -1, "SSL") ||
        g_strstr_len(m, -1, "certificate")) {
        load_error_page(web_view, "ssl", failing_uri);
        return TRUE;
    }

    /* Detect "offline" — either the network monitor says there's no network,
     * or the failure looks like a connectivity/DNS error (the classic offline
     * symptoms: host can't be resolved or the server can't be reached). */
    GNetworkMonitor *mon = g_network_monitor_get_default();
    gboolean no_net = !g_network_monitor_get_network_available(mon);
    gboolean conn_error =
        g_strstr_len(m, -1, "resolve") || g_strstr_len(m, -1, "host") ||
        g_strstr_len(m, -1, "Name or service") || g_strstr_len(m, -1, "name resolution") ||
        g_strstr_len(m, -1, "unreachable") || g_strstr_len(m, -1, "Could not connect") ||
        g_strstr_len(m, -1, "Connection refused") || g_strstr_len(m, -1, "timed out") ||
        g_strstr_len(m, -1, "network") || *m == '\0';

    if (no_net || conn_error) {
        load_offline_page(web_view, failing_uri);
        return TRUE;
    }

    load_error_page(web_view, "generic", failing_uri);
    return TRUE;
}

/* TLS/certificate load failures → SSL error page */
static gboolean
on_load_failed_tls(WebKitWebView *web_view, const char *failing_uri,
    GTlsCertificate *cert, GTlsCertificateFlags errors, gpointer user_data)
{
    (void)cert; (void)errors; (void)user_data;
    load_error_page(web_view, "ssl", failing_uri);
    return TRUE;
}

/* Retry message from offline page */
static void
on_retry_message(WebKitUserContentManager *ucm, JSCValue *js_result, gpointer user_data)
{
    (void)ucm;
    (void)js_result;
    BrowserTab *self = BROWSER_TAB(user_data);

    /* Reload whatever URI is currently shown (the failing URI) */
    const char *uri = webkit_web_view_get_uri(self->web_view);
    if (uri && *uri && !g_str_has_prefix(uri, "about:")) {
        webkit_web_view_load_uri(self->web_view, uri);
    } else {
        webkit_web_view_reload(self->web_view);
    }
}

/* Go-back message from the error page */
static void
on_goback_message(WebKitUserContentManager *ucm, JSCValue *js_result, gpointer user_data)
{
    (void)ucm; (void)js_result;
    BrowserTab *self = BROWSER_TAB(user_data);
    if (webkit_web_view_can_go_back(self->web_view))
        webkit_web_view_go_back(self->web_view);
    else
        webkit_web_view_load_uri(self->web_view, "open://home");
}

static void
browser_tab_init(BrowserTab *self)
{
    self->zoom_level = 1.0;
    self->is_private = FALSE;
    self->is_pinned = FALSE;
    self->is_muted = FALSE;

    gtk_orientable_set_orientation(GTK_ORIENTABLE(self), GTK_ORIENTATION_VERTICAL);

    /* Find bar */
    self->find_bar = create_find_bar(self);
    /* find bar is added as an overlay over the WebView in browser_tab_new */

    /* WebView will be created in browser_tab_new */
}

/* Custom URI scheme handler for open:// pages (startup, etc.) */
static void
on_uri_scheme_request(WebKitURISchemeRequest *request, gpointer user_data)
{
    (void)user_data;
    const char *uri = webkit_uri_scheme_request_get_uri(request);

    /* open://welcome -> serve welcome.html (match on the URI host) */
    if (uri && g_strstr_len(uri, -1, "welcome")) {
        GBytes *wb = g_resources_lookup_data("/com/openbrowser/welcome.html",
            G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
        if (wb) {
            gsize wsize;
            const char *wdata = g_bytes_get_data(wb, &wsize);
            GInputStream *wstream = g_memory_input_stream_new_from_data(g_strdup(wdata), wsize, g_free);
            webkit_uri_scheme_request_finish(request, wstream, wsize, "text/html");
            g_object_unref(wstream);
            g_bytes_unref(wb);
            return;
        }
    }

    /* open://login.json -> serve the Lottie animation data (case-insensitive) */
    if (uri) {
        char *lower = g_ascii_strdown(uri, -1);
        gboolean is_login = strstr(lower, "login.json") != NULL;
        g_free(lower);
        if (is_login) {
            GBytes *lb = g_resources_lookup_data("/com/openbrowser/Login.json",
                G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
            if (lb) {
                gsize lsize;
                const char *ldata = g_bytes_get_data(lb, &lsize);
                GInputStream *lstream = g_memory_input_stream_new_from_data(g_strdup(ldata), lsize, g_free);
                webkit_uri_scheme_request_finish(request, lstream, lsize, "application/json");
                g_object_unref(lstream);
                g_bytes_unref(lb);
                return;
            }
        }
    }

    /* open://.../footer.svg -> serve the footer SVG */
    if (uri) {
        char *lower2 = g_ascii_strdown(uri, -1);
        gboolean is_footer = strstr(lower2, "footer.svg") != NULL;
        g_free(lower2);
        if (is_footer) {
            GBytes *fb = g_resources_lookup_data("/com/openbrowser/icons/footer.svg",
                G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
            if (fb) {
                gsize fsize;
                const char *fdata = g_bytes_get_data(fb, &fsize);
                GInputStream *fstream = g_memory_input_stream_new_from_data(g_strdup(fdata), fsize, g_free);
                webkit_uri_scheme_request_finish(request, fstream, fsize, "image/svg+xml");
                g_object_unref(fstream);
                g_bytes_unref(fb);
                return;
            }
        }
    }

    /* open://home (or anything else) -> serve startup.html */
    {
        GBytes *bytes = g_resources_lookup_data("/com/openbrowser/startup.html",
            G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
        if (bytes) {
            gsize size;
            const char *data = g_bytes_get_data(bytes, &size);
            GInputStream *stream = g_memory_input_stream_new_from_data(
                g_strdup(data), size, g_free);
            webkit_uri_scheme_request_finish(request, stream, size, "text/html");
            g_object_unref(stream);
            g_bytes_unref(bytes);
            return;
        }
    }

    /* Fallback: 404 */
    GError *error = g_error_new(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Not found");
    webkit_uri_scheme_request_finish_error(request, error);
    g_error_free(error);
}

/* Inject the performance lazy-load script into a content manager */
static void
add_perf_script(WebKitUserContentManager *ucm)
{
    WebKitUserScript *perf_script = webkit_user_script_new(
        "(() => {"
        "  const obs = new MutationObserver((muts) => {"
        "    for (const m of muts) {"
        "      for (const n of m.addedNodes) {"
        "        if (n.tagName === 'IMG' && !n.loading) n.loading = 'lazy';"
        "        if (n.querySelectorAll) {"
        "          n.querySelectorAll('img:not([loading])').forEach(i => i.loading = 'lazy');"
        "        }"
        "      }"
        "    }"
        "  });"
        "  obs.observe(document.documentElement, {childList:true, subtree:true});"
        "  document.querySelectorAll('img:not([loading])').forEach(i => i.loading = 'lazy');"
        "})();",
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END,
        NULL, NULL);
    webkit_user_content_manager_add_script(ucm, perf_script);
    webkit_user_script_unref(perf_script);
}

/* Re-apply extensions live: clear injected content, re-add perf + enabled
 * extensions, then reload so script changes take effect immediately. */
void
browser_tab_reapply_extensions(BrowserTab *self)
{
    g_return_if_fail(BROWSER_IS_TAB(self));
    WebKitUserContentManager *ucm = webkit_web_view_get_user_content_manager(self->web_view);
    webkit_user_content_manager_remove_all_scripts(ucm);
    webkit_user_content_manager_remove_all_style_sheets(ucm);
    add_perf_script(ucm);
    extension_manager_apply(extension_manager_get_default(), ucm);
    webkit_web_view_reload(self->web_view);
}

BrowserTab *
browser_tab_new(const char *uri, gboolean is_private)
{
    BrowserTab *self = g_object_new(BROWSER_TYPE_TAB, NULL);
    self->is_private = is_private;

    /* Create web context with persistent storage */
    WebKitWebContext *context;
    if (is_private) {
        WebKitNetworkSession *session = webkit_network_session_new_ephemeral();
        self->web_view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
            "network-session", session,
            NULL));
        g_object_unref(session);
        context = webkit_web_view_get_context(self->web_view);
    } else {
        /* Use persistent network session with cookie storage */
        static WebKitNetworkSession *persistent_session = NULL;
        if (!persistent_session) {
            char *data_dir = g_build_filename(g_get_user_data_dir(), "openbrowser", NULL);
            char *cache_dir = g_build_filename(g_get_user_cache_dir(), "openbrowser", NULL);

            /* Ensure directories exist */
            g_mkdir_with_parents(data_dir, 0755);
            g_mkdir_with_parents(cache_dir, 0755);

            persistent_session = webkit_network_session_new(data_dir, cache_dir);

            /* Configure persistent cookies */
            WebKitCookieManager *cookie_mgr = webkit_network_session_get_cookie_manager(persistent_session);
            char *cookie_file = g_build_filename(data_dir, "cookies.sqlite", NULL);
            webkit_cookie_manager_set_persistent_storage(cookie_mgr, cookie_file,
                WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
            webkit_cookie_manager_set_accept_policy(cookie_mgr,
                WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);

            /* Enable Intelligent Tracking Prevention for privacy */
            webkit_network_session_set_itp_enabled(persistent_session, TRUE);

            /* Enable favicon database so tabs can show site logos */
            WebKitWebsiteDataManager *wdm = webkit_network_session_get_website_data_manager(persistent_session);
            webkit_website_data_manager_set_favicons_enabled(wdm, TRUE);

            /* Prefetch DNS for faster navigation on slow connections */
            webkit_network_session_prefetch_dns(persistent_session, "www.google.com");
            webkit_network_session_prefetch_dns(persistent_session, "www.youtube.com");
            webkit_network_session_prefetch_dns(persistent_session, "twitter.com");
            webkit_network_session_prefetch_dns(persistent_session, "x.com");
            webkit_network_session_prefetch_dns(persistent_session, "www.instagram.com");
            webkit_network_session_prefetch_dns(persistent_session, "www.facebook.com");
            webkit_network_session_prefetch_dns(persistent_session, "www.reddit.com");
            /* Common CDNs */
            webkit_network_session_prefetch_dns(persistent_session, "cdn.jsdelivr.net");
            webkit_network_session_prefetch_dns(persistent_session, "cdnjs.cloudflare.com");
            webkit_network_session_prefetch_dns(persistent_session, "fonts.googleapis.com");
            webkit_network_session_prefetch_dns(persistent_session, "ajax.googleapis.com");

            g_free(data_dir);
            g_free(cache_dir);
            g_free(cookie_file);
        }

        self->web_view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
            "network-session", persistent_session,
            NULL));
        context = webkit_web_view_get_context(self->web_view);
    }

    /* Register custom URI scheme for startup page (only once) */
    static gboolean scheme_registered = FALSE;
    if (!scheme_registered) {
        webkit_web_context_register_uri_scheme(context, "open",
            (WebKitURISchemeRequestCallback)on_uri_scheme_request, NULL, NULL);

        /* Mark as local/secure so navigation to https:// is allowed */
        WebKitSecurityManager *security = webkit_web_context_get_security_manager(context);
        webkit_security_manager_register_uri_scheme_as_local(security, "open");
        webkit_security_manager_register_uri_scheme_as_cors_enabled(security, "open");

        /* Aggressive disk+memory caching — critical for slow connections */
        webkit_web_context_set_cache_model(context, WEBKIT_CACHE_MODEL_WEB_BROWSER);

        scheme_registered = TRUE;
    }

    /* Configure settings */
    WebKitSettings *settings = webkit_web_view_get_settings(self->web_view);
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, TRUE);
    webkit_settings_set_enable_smooth_scrolling(settings, TRUE);
    webkit_settings_set_enable_media(settings, TRUE);
    webkit_settings_set_enable_webgl(settings, TRUE);
    webkit_settings_set_enable_developer_extras(settings, TRUE);
    webkit_settings_set_allow_top_navigation_to_data_urls(settings, TRUE);

    /* Use default WebKitGTK user agent for best compatibility */
    /* webkit_settings_set_user_agent - intentionally not set, using WebKitGTK default */

    /* Performance: GPU acceleration enabled — DMA-BUF disabled separately to avoid iGPU blink */
    webkit_settings_set_hardware_acceleration_policy(settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS);
    webkit_settings_set_enable_back_forward_navigation_gestures(settings, TRUE);
    webkit_settings_set_enable_page_cache(settings, TRUE);

    /* Speed optimizations */
    webkit_settings_set_enable_html5_database(settings, TRUE);
    webkit_settings_set_enable_html5_local_storage(settings, TRUE);
    webkit_settings_set_enable_media_capabilities(settings, TRUE);
    webkit_settings_set_enable_encrypted_media(settings, TRUE);
    webkit_settings_set_enable_media_stream(settings, TRUE);
    webkit_settings_set_enable_mediasource(settings, TRUE);

    /* Rendering performance */
    webkit_settings_set_draw_compositing_indicators(settings, FALSE);
    webkit_settings_set_enable_write_console_messages_to_stdout(settings, FALSE);

    /* Allow our internal (local-scheme) new-tab page to call external APIs
     * (Hacker News, Yahoo Finance, weather) directly without a CORS proxy.
     * This privilege only applies to local-scheme documents (our bundled
     * open:// pages), not to regular https websites. */
    webkit_settings_set_allow_file_access_from_file_urls(settings, TRUE);
    webkit_settings_set_allow_universal_access_from_file_urls(settings, TRUE);

    /* Prefer dark color scheme for websites via web view settings */
    WebKitUserContentManager *ucm = webkit_web_view_get_user_content_manager(self->web_view);

    /* Inject performance script + enabled extensions */
    add_perf_script(ucm);
    extension_manager_apply(extension_manager_get_default(), ucm);

    /* Register message handlers for startup page navigation */
    g_signal_connect(ucm, "script-message-received::passwordSave",
        G_CALLBACK(on_password_save_message), self);
    webkit_user_content_manager_register_script_message_handler(ucm, "passwordSave", NULL);

    /* Navigate handler: startup page sends URLs here for direct navigation */
    g_signal_connect(ucm, "script-message-received::navigate",
        G_CALLBACK(on_navigate_message), self);
    webkit_user_content_manager_register_script_message_handler(ucm, "navigate", NULL);

    /* Retry handler: offline page's "Try Again" button */
    g_signal_connect(ucm, "script-message-received::retry",
        G_CALLBACK(on_retry_message), self);
    webkit_user_content_manager_register_script_message_handler(ucm, "retry", NULL);

    /* Go-back handler: error page's "Go Back" button */
    g_signal_connect(ucm, "script-message-received::goback",
        G_CALLBACK(on_goback_message), self);
    webkit_user_content_manager_register_script_message_handler(ucm, "goback", NULL);

    /* Favorites: new-tab page saves the user's favorites here */
    g_signal_connect(ucm, "script-message-received::favSave",
        G_CALLBACK(on_favorites_save_message), self);
    webkit_user_content_manager_register_script_message_handler(ucm, "favSave", NULL);

    /* New-tab preferences: Customise dialog saves its settings here */
    g_signal_connect(ucm, "script-message-received::prefsSave",
        G_CALLBACK(on_newtab_prefs_save_message), self);
    webkit_user_content_manager_register_script_message_handler(ucm, "prefsSave", NULL);

    /* New-tab content cache: news/weather/markets data persisted for instant loads */
    g_signal_connect(ucm, "script-message-received::cacheSave",
        G_CALLBACK(on_newtab_cache_save_message), self);
    webkit_user_content_manager_register_script_message_handler(ucm, "cacheSave", NULL);

    /* Inject saved favorites into the new-tab page once it has loaded */
    g_signal_connect(self->web_view, "load-changed",
        G_CALLBACK(on_startup_load_changed), self);

    /* Show offline/error pages on network failures */
    g_signal_connect(self->web_view, "load-failed", G_CALLBACK(on_load_failed), self);
    g_signal_connect(self->web_view, "load-failed-with-tls-errors", G_CALLBACK(on_load_failed_tls), self);

    /* Find controller */
    self->find_controller = webkit_web_view_get_find_controller(self->web_view);

    /* NOTE: Auto-fill disabled — injecting JS into SPA sites (Instagram, Twitter)
     * breaks React state and invalidates sessions causing login loops.
     * TODO: Re-enable with proper detection (only on non-SPA login pages) */
    /* g_signal_connect(self->web_view, "load-changed",
        G_CALLBACK(on_page_load_for_autofill), self); */

    /* Add WebView to the tab inside an overlay so the find bar can float */
    gtk_widget_set_vexpand(GTK_WIDGET(self->web_view), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(self->web_view), TRUE);
    GtkWidget *overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), GTK_WIDGET(self->web_view));
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), self->find_bar);
    gtk_box_append(GTK_BOX(self), overlay);

    /* Navigate to URI */
    if (uri && *uri && g_strcmp0(uri, "about:blank") != 0) {
        webkit_web_view_load_uri(self->web_view, uri);
    } else if (!uri) {
        /* No URI given — load startup page */
        webkit_web_view_load_uri(self->web_view, "open://home");
    } else {
        /* about:blank — explicitly load it to initialize the WebView */
        webkit_web_view_load_uri(self->web_view, "about:blank");
    }

    return self;
}

BrowserTab *
browser_tab_new_related(WebKitWebView *related_view)
{
    BrowserTab *self = g_object_new(BROWSER_TYPE_TAB, NULL);
    self->is_private = FALSE;

    /* Create a new WebView related to the source — shares process, cookies, session.
     * IMPORTANT: Do NOT specify network-session when using related-view,
     * it's inherited automatically. Specifying both causes conflicts. */
    self->web_view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "related-view", related_view,
        NULL));

    /* Configure settings same as normal tabs */
    WebKitSettings *settings = webkit_web_view_get_settings(self->web_view);
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, TRUE);
    webkit_settings_set_enable_smooth_scrolling(settings, TRUE);
    webkit_settings_set_enable_media(settings, TRUE);
    webkit_settings_set_enable_webgl(settings, TRUE);
    webkit_settings_set_hardware_acceleration_policy(settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS);
    webkit_settings_set_enable_back_forward_navigation_gestures(settings, TRUE);

    /* Find controller */
    self->find_controller = webkit_web_view_get_find_controller(self->web_view);

    /* Add WebView to the tab inside an overlay (find bar floats on top) */
    gtk_widget_set_vexpand(GTK_WIDGET(self->web_view), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(self->web_view), TRUE);
    GtkWidget *overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), GTK_WIDGET(self->web_view));
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), self->find_bar);
    gtk_box_append(GTK_BOX(self), overlay);

    /* Don't load anything — the opener will navigate this view */
    return self;
}

WebKitWebView *
browser_tab_get_web_view(BrowserTab *self)
{
    g_return_val_if_fail(BROWSER_IS_TAB(self), NULL);
    return self->web_view;
}

const char *
browser_tab_get_uri(BrowserTab *self)
{
    g_return_val_if_fail(BROWSER_IS_TAB(self), NULL);
    return webkit_web_view_get_uri(self->web_view);
}

const char *
browser_tab_get_title(BrowserTab *self)
{
    g_return_val_if_fail(BROWSER_IS_TAB(self), NULL);
    const char *title = webkit_web_view_get_title(self->web_view);
    return title ? title : "New Tab";
}

gboolean
browser_tab_is_loading(BrowserTab *self)
{
    g_return_val_if_fail(BROWSER_IS_TAB(self), FALSE);
    return webkit_web_view_is_loading(self->web_view);
}

gboolean
browser_tab_is_private(BrowserTab *self)
{
    g_return_val_if_fail(BROWSER_IS_TAB(self), FALSE);
    return self->is_private;
}

gboolean
browser_tab_is_pinned(BrowserTab *self)
{
    g_return_val_if_fail(BROWSER_IS_TAB(self), FALSE);
    return self->is_pinned;
}

gboolean
browser_tab_is_muted(BrowserTab *self)
{
    g_return_val_if_fail(BROWSER_IS_TAB(self), FALSE);
    return self->is_muted;
}

void
browser_tab_set_pinned(BrowserTab *self, gboolean pinned)
{
    g_return_if_fail(BROWSER_IS_TAB(self));
    self->is_pinned = pinned;
}

void
browser_tab_set_muted(BrowserTab *self, gboolean muted)
{
    g_return_if_fail(BROWSER_IS_TAB(self));
    self->is_muted = muted;
    webkit_web_view_set_is_muted(self->web_view, muted);
}

double
browser_tab_get_zoom_level(BrowserTab *self)
{
    g_return_val_if_fail(BROWSER_IS_TAB(self), 1.0);
    return self->zoom_level;
}

void
browser_tab_set_zoom_level(BrowserTab *self, double level)
{
    g_return_if_fail(BROWSER_IS_TAB(self));
    self->zoom_level = CLAMP(level, 0.25, 5.0);
    webkit_web_view_set_zoom_level(self->web_view, self->zoom_level);
}

void
browser_tab_navigate(BrowserTab *self, const char *uri)
{
    g_return_if_fail(BROWSER_IS_TAB(self));
    g_return_if_fail(uri != NULL);

    /* Check if it's a search query or URL */
    if (!g_str_has_prefix(uri, "http://") && !g_str_has_prefix(uri, "https://") &&
        !g_str_has_prefix(uri, "file://") && !g_str_has_prefix(uri, "about:")) {
        if (g_strstr_len(uri, -1, ".") && !g_strstr_len(uri, -1, " ")) {
            /* Likely a URL without scheme */
            char *full_uri = g_strdup_printf("https://%s", uri);
            webkit_web_view_load_uri(self->web_view, full_uri);
            g_free(full_uri);
        } else {
            /* Treat as search query - use configured search engine */
            SettingsManager *settings = settings_manager_get_default();
            const char *search_url_fmt = settings_manager_get_search_url(settings);
            char *encoded = g_uri_escape_string(uri, NULL, FALSE);
            char *search_uri = g_strdup_printf(search_url_fmt, encoded);
            webkit_web_view_load_uri(self->web_view, search_uri);
            g_free(encoded);
            g_free(search_uri);
        }
    } else {
        webkit_web_view_load_uri(self->web_view, uri);
    }
}

void
browser_tab_go_back(BrowserTab *self)
{
    g_return_if_fail(BROWSER_IS_TAB(self));
    if (webkit_web_view_can_go_back(self->web_view))
        webkit_web_view_go_back(self->web_view);
}

void
browser_tab_go_forward(BrowserTab *self)
{
    g_return_if_fail(BROWSER_IS_TAB(self));
    if (webkit_web_view_can_go_forward(self->web_view))
        webkit_web_view_go_forward(self->web_view);
}

void
browser_tab_reload(BrowserTab *self)
{
    g_return_if_fail(BROWSER_IS_TAB(self));
    webkit_web_view_reload(self->web_view);
}

void
browser_tab_hard_reload(BrowserTab *self)
{
    g_return_if_fail(BROWSER_IS_TAB(self));
    webkit_web_view_reload_bypass_cache(self->web_view);
}

void
browser_tab_stop_loading(BrowserTab *self)
{
    g_return_if_fail(BROWSER_IS_TAB(self));
    webkit_web_view_stop_loading(self->web_view);
}

void
browser_tab_find_text(BrowserTab *self, const char *text)
{
    g_return_if_fail(BROWSER_IS_TAB(self));
    gtk_widget_set_visible(self->find_bar, TRUE);
    if (text && *text) {
        gtk_editable_set_text(GTK_EDITABLE(self->find_entry), text);
    }
    gtk_widget_grab_focus(self->find_entry);
}

void
browser_tab_find_next(BrowserTab *self)
{
    g_return_if_fail(BROWSER_IS_TAB(self));
    webkit_find_controller_search_next(self->find_controller);
}

void
browser_tab_find_previous(BrowserTab *self)
{
    g_return_if_fail(BROWSER_IS_TAB(self));
    webkit_find_controller_search_previous(self->find_controller);
}

void
browser_tab_find_close(BrowserTab *self)
{
    g_return_if_fail(BROWSER_IS_TAB(self));
    gtk_widget_set_visible(self->find_bar, FALSE);
    webkit_find_controller_search_finish(self->find_controller);
}
