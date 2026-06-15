#include "browser-tab.h"
#include "settings-manager.h"
#include "password-manager.h"
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
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(bar, "find-bar");
    gtk_widget_set_visible(bar, FALSE);

    self->find_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(self->find_entry), "Find in page...");
    gtk_widget_add_css_class(self->find_entry, "find-entry");
    gtk_widget_set_hexpand(self->find_entry, TRUE);
    gtk_box_append(GTK_BOX(bar), self->find_entry);

    g_signal_connect(self->find_entry, "changed", G_CALLBACK(on_find_entry_changed), self);

    GtkWidget *prev_btn = gtk_button_new_from_icon_name("go-up-symbolic");
    gtk_widget_add_css_class(prev_btn, "nav-button");
    gtk_box_append(GTK_BOX(bar), prev_btn);
    g_signal_connect(prev_btn, "clicked", G_CALLBACK(on_find_prev_clicked), self);

    GtkWidget *next_btn = gtk_button_new_from_icon_name("go-down-symbolic");
    gtk_widget_add_css_class(next_btn, "nav-button");
    gtk_box_append(GTK_BOX(bar), next_btn);
    g_signal_connect(next_btn, "clicked", G_CALLBACK(on_find_next_clicked), self);

    GtkWidget *close_btn = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(close_btn, "nav-button");
    gtk_box_append(GTK_BOX(bar), close_btn);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_find_close_clicked), self);

    return bar;
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
        /* Inject auto-fill JavaScript */
        char *escaped_user = g_strescape(entry->username, NULL);
        char *escaped_pass = g_strescape(entry->password, NULL);

        char *js = g_strdup_printf(
            "(() => {"
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
            "      userField.value = '%s';"
            "      userField.dispatchEvent(new Event('input', {bubbles:true}));"
            "    }"
            "    passField.value = '%s';"
            "    passField.dispatchEvent(new Event('input', {bubbles:true}));"
            "  }"
            "})();",
            escaped_user, escaped_pass);

        webkit_web_view_evaluate_javascript(web_view, js, -1, NULL, NULL, NULL, NULL, NULL);

        g_free(js);
        g_free(escaped_user);
        g_free(escaped_pass);
    }

    g_uri_unref(guri);
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
    gtk_box_append(GTK_BOX(self), self->find_bar);

    /* WebView will be created in browser_tab_new */
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
            persistent_session = webkit_network_session_new(data_dir, cache_dir);

            /* Configure persistent cookies */
            WebKitCookieManager *cookie_mgr = webkit_network_session_get_cookie_manager(persistent_session);
            char *cookie_file = g_build_filename(data_dir, "cookies.sqlite", NULL);
            webkit_cookie_manager_set_persistent_storage(cookie_mgr, cookie_file,
                WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
            webkit_cookie_manager_set_accept_policy(cookie_mgr,
                WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);

            g_free(data_dir);
            g_free(cache_dir);
            g_free(cookie_file);
        }

        self->web_view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
            "network-session", persistent_session,
            NULL));
        context = webkit_web_view_get_context(self->web_view);
    }
    (void)context;

    /* Configure settings */
    WebKitSettings *settings = webkit_web_view_get_settings(self->web_view);
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_enable_smooth_scrolling(settings, TRUE);
    webkit_settings_set_enable_media(settings, TRUE);
    webkit_settings_set_enable_webgl(settings, TRUE);
    webkit_settings_set_enable_developer_extras(settings, TRUE);
    webkit_settings_set_user_agent_with_application_details(settings, "OpenBrowser", "1.0");

    /* Prefer dark color scheme for websites */
    WebKitUserContentManager *ucm = webkit_web_view_get_user_content_manager(self->web_view);
    WebKitUserStyleSheet *dark_css = webkit_user_style_sheet_new(
        ":root { color-scheme: dark; }",
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_STYLE_LEVEL_USER,
        NULL, NULL);
    webkit_user_content_manager_add_style_sheet(ucm, dark_css);
    webkit_user_style_sheet_unref(dark_css);

    /* Password auto-save: inject script to detect form submissions */
    g_signal_connect(ucm, "script-message-received::passwordSave",
        G_CALLBACK(on_password_save_message), self);
    webkit_user_content_manager_register_script_message_handler(ucm, "passwordSave", NULL);

    WebKitUserScript *pw_script = webkit_user_script_new(
        /* JavaScript to detect and capture login form submissions */
        "(() => {"
        "  let savedUser = '';"
        ""
        "  /* Capture all visible input values on the page */"
        "  function capturePage() {"
        "    let username = '', password = '', email = '', phone = '';"
        "    const inputs = document.querySelectorAll('input');"
        "    inputs.forEach(inp => {"
        "      if (!inp.value || inp.offsetParent === null) return;"
        "      const type = (inp.type || '').toLowerCase();"
        "      const name = (inp.name || inp.id || inp.autocomplete || '').toLowerCase();"
        "      if (type === 'password') password = inp.value;"
        "      else if (type === 'email') email = inp.value;"
        "      else if (type === 'tel') phone = inp.value;"
        "      else if (type === 'text' || type === '') {"
        "        if (name.includes('user') || name.includes('login') || name.includes('email')"
        "            || name.includes('name') || name.includes('account') || name.includes('id')"
        "            || name.includes('identifier') || name.includes('handle')) {"
        "          username = inp.value;"
        "        }"
        "      }"
        "    });"
        "    const user = username || email || phone;"
        "    if (user) savedUser = user;"
        "    if (password && savedUser) {"
        "      const site = window.location.hostname;"
        "      window.webkit.messageHandlers.passwordSave.postMessage("
        "        JSON.stringify({site: site, username: savedUser, password: password})"
        "      );"
        "    }"
        "  }"
        ""
        "  /* Watch for form submits */"
        "  document.addEventListener('submit', () => {"
        "    setTimeout(capturePage, 50);"
        "  }, true);"
        ""
        "  /* Watch for ANY button/link click (multi-step logins like Google) */"
        "  document.addEventListener('click', (e) => {"
        "    const t = e.target.closest('button, [role=button], input[type=submit], a');"
        "    if (t) setTimeout(capturePage, 200);"
        "  }, true);"
        ""
        "  /* Watch for Enter key press in inputs */"
        "  document.addEventListener('keydown', (e) => {"
        "    if (e.key === 'Enter' && e.target.tagName === 'INPUT') {"
        "      setTimeout(capturePage, 200);"
        "    }"
        "  }, true);"
        ""
        "  /* Also capture on page unload (navigation away from login page) */"
        "  window.addEventListener('beforeunload', capturePage);"
        ""
        "  /* Remember email/username from first step of multi-step login */"
        "  document.addEventListener('input', (e) => {"
        "    if (e.target.tagName === 'INPUT') {"
        "      const type = (e.target.type || '').toLowerCase();"
        "      const name = (e.target.name || e.target.id || '').toLowerCase();"
        "      if ((type === 'email' || type === 'text' || type === 'tel') && e.target.value) {"
        "        if (type === 'email' || name.includes('user') || name.includes('email')"
        "            || name.includes('login') || name.includes('identifier')"
        "            || name.includes('account') || name.includes('phone')) {"
        "          savedUser = e.target.value;"
        "        }"
        "      }"
        "    }"
        "  }, true);"
        ""
        "  /* Store username in sessionStorage for multi-page flows */"
        "  try {"
        "    const stored = sessionStorage.getItem('_ob_user');"
        "    if (stored) savedUser = stored;"
        "    const origSet = sessionStorage.setItem.bind(sessionStorage);"
        "    setInterval(() => {"
        "      if (savedUser) origSet('_ob_user', savedUser);"
        "    }, 1000);"
        "  } catch(e) {}"
        "})();",
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END,
        NULL, NULL);
    webkit_user_content_manager_add_script(ucm, pw_script);
    webkit_user_script_unref(pw_script);

    /* Find controller */
    self->find_controller = webkit_web_view_get_find_controller(self->web_view);

    /* Connect auto-fill on page load */
    g_signal_connect(self->web_view, "load-changed",
        G_CALLBACK(on_page_load_for_autofill), self);

    /* Add WebView to the tab */
    gtk_widget_set_vexpand(GTK_WIDGET(self->web_view), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(self->web_view), TRUE);
    gtk_box_append(GTK_BOX(self), GTK_WIDGET(self->web_view));

    /* Navigate to URI */
    if (uri && *uri && g_strcmp0(uri, "about:blank") != 0) {
        webkit_web_view_load_uri(self->web_view, uri);
    } else {
        /* Load startup page from GResource, with dynamic search engine */
        GBytes *bytes = g_resources_lookup_data("/com/openbrowser/startup.html",
            G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
        if (bytes) {
            gsize size;
            const char *data = g_bytes_get_data(bytes, &size);

            /* Get current search engine URL and replace placeholder in HTML */
            SettingsManager *settings = settings_manager_get_default();
            const char *search_url = settings_manager_get_search_url(settings);

            /* Extract base URL (everything before ?q=%s or ?p=%s etc) */
            char *base_url = g_strdup(search_url);
            char *qmark = strchr(base_url, '?');

            /* Build form action and input name */
            char *form_action = NULL;
            const char *input_name = "q";
            if (qmark) {
                *qmark = '\0'; /* base_url now has just the domain+path */
                form_action = g_strdup(base_url);
                /* Figure out param name from the format string */
                char *param_start = qmark + 1; /* points into original search_url after ? */
                /* search_url is like "https://...?q=%s" or "?p=%s" */
                const char *fmt_pos = strchr(search_url, '?');
                if (fmt_pos) {
                    fmt_pos++; /* skip ? */
                    if (g_str_has_prefix(fmt_pos, "p=")) input_name = "p";
                    else if (g_str_has_prefix(fmt_pos, "text=")) input_name = "text";
                    else input_name = "q";
                }
                (void)param_start;
            } else {
                form_action = g_strdup(base_url);
            }
            g_free(base_url);

            /* Get engine name for placeholder text */
            SearchEngine engine = settings_manager_get_search_engine(settings);
            const char *engine_name = "the web";
            switch (engine) {
                case SEARCH_GOOGLE: engine_name = "Google"; break;
                case SEARCH_DUCKDUCKGO: engine_name = "DuckDuckGo"; break;
                case SEARCH_BING: engine_name = "Bing"; break;
                case SEARCH_BRAVE: engine_name = "Brave Search"; break;
                case SEARCH_STARTPAGE: engine_name = "Startpage"; break;
                case SEARCH_YAHOO: engine_name = "Yahoo"; break;
                case SEARCH_YANDEX: engine_name = "Yandex"; break;
                default: engine_name = "the web"; break;
            }

            /* Replace placeholders in HTML */
            char *html = g_strdup(data);
            char *placeholder_text = g_strdup_printf("Search with %s...", engine_name);

            /* Replace form action */
            char *tmp = g_regex_replace_literal(
                g_regex_new("https://duckduckgo\\.com/", 0, 0, NULL),
                html, -1, 0, form_action, 0, NULL);
            if (tmp) { g_free(html); html = tmp; }

            /* Replace input name if needed */
            if (g_strcmp0(input_name, "q") != 0) {
                char *name_replace = g_strdup_printf("name=\"%s\"", input_name);
                tmp = g_regex_replace_literal(
                    g_regex_new("name=\"q\"", 0, 0, NULL),
                    html, -1, 0, name_replace, 0, NULL);
                if (tmp) { g_free(html); html = tmp; }
                g_free(name_replace);
            }

            /* Replace placeholder text */
            tmp = g_regex_replace_literal(
                g_regex_new("Search with DuckDuckGo\\.\\.\\.", 0, 0, NULL),
                html, -1, 0, placeholder_text, 0, NULL);
            if (tmp) { g_free(html); html = tmp; }

            webkit_web_view_load_html(self->web_view, html, "https://unpkg.com");

            g_free(html);
            g_free(form_action);
            g_free(placeholder_text);
            g_bytes_unref(bytes);
        } else {
            webkit_web_view_load_html(self->web_view,
                "<html><body style='background:#181818;color:#ccc;font-family:sans-serif;"
                "display:flex;align-items:center;justify-content:center;height:100vh'>"
                "<h1>OpenBrowser</h1></body></html>", NULL);
        }
    }

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
