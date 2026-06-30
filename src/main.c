#include <adwaita.h>
#include <webkit/webkit.h>
#include "browser-window.h"
#include "settings-manager.h"

static void
on_activate(AdwApplication *app, gpointer user_data)
{
    (void)user_data;

    /* If a window already exists, just present it */
    GtkWindow *existing = gtk_application_get_active_window(GTK_APPLICATION(app));
    if (existing) {
        gtk_window_present(existing);
        return;
    }

    /* Load CSS */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/com/openbrowser/style.css");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    /* Register bundled symbolic icons (e.g. cookie-symbolic) */
    gtk_icon_theme_add_resource_path(
        gtk_icon_theme_get_for_display(gdk_display_get_default()),
        "/com/openbrowser/icons");

    /* Apply dark theme */
    AdwStyleManager *style_mgr = adw_style_manager_get_default();
    adw_style_manager_set_color_scheme(style_mgr, ADW_COLOR_SCHEME_FORCE_DARK);

    /* Create main window */
    BrowserWindow *window = browser_window_new(app);

    /* Set window icon */
    gtk_window_set_icon_name(GTK_WINDOW(window), "io.github.ramm_fr.OpenBrowser");

    gtk_window_present(GTK_WINDOW(window));
}

static void
on_open(GApplication *app, GFile **files, int n_files, const char *hint, gpointer user_data)
{
    (void)hint;
    (void)user_data;

    /* Activate ensures window exists (reuses existing) */
    g_application_activate(app);

    GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(app));
    if (window && BROWSER_IS_WINDOW(window) && n_files > 0) {
        for (int i = 0; i < n_files; i++) {
            char *uri = g_file_get_uri(files[i]);
            browser_window_new_tab(BROWSER_WINDOW(window), uri);
            g_free(uri);
        }
        gtk_window_present(window);
    }
}

static int
handle_command_line(GApplication *app, GApplicationCommandLine *cmdline, gpointer user_data)
{
    (void)user_data;

    GVariantDict *options = g_application_command_line_get_options_dict(cmdline);

    gboolean private_mode = FALSE;
    g_variant_dict_lookup(options, "private", "b", &private_mode);

    if (private_mode) {
        g_object_set_data(G_OBJECT(app), "private-mode", GINT_TO_POINTER(1));
    }

    /* Get the command-line arguments (URLs passed from external apps) */
    int argc;
    char **argv = g_application_command_line_get_arguments(cmdline, &argc);

    /* Collect URIs from arguments (skip argv[0] which is the program name) */
    GPtrArray *uris = g_ptr_array_new_with_free_func(g_free);
    for (int i = 1; i < argc; i++) {
        if (argv[i] && argv[i][0] != '-') {
            /* If it looks like a URL or file path, add it */
            if (g_str_has_prefix(argv[i], "http://") ||
                g_str_has_prefix(argv[i], "https://") ||
                g_str_has_prefix(argv[i], "file://") ||
                g_str_has_prefix(argv[i], "/")) {
                g_ptr_array_add(uris, g_strdup(argv[i]));
            } else if (g_strstr_len(argv[i], -1, ".")) {
                /* Probably a URL without scheme */
                g_ptr_array_add(uris, g_strdup_printf("https://%s", argv[i]));
            }
        }
    }
    g_strfreev(argv);

    /* Activate the app (creates window if needed, or presents existing one) */
    g_application_activate(app);

    /* Get the window (existing or newly created) */
    GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(app));

    /* If URIs were passed, open them in tabs */
    if (uris->len > 0 && window && BROWSER_IS_WINDOW(window)) {
        for (guint i = 0; i < uris->len; i++) {
            const char *uri = g_ptr_array_index(uris, i);
            browser_window_new_tab(BROWSER_WINDOW(window), uri);
        }
        gtk_window_present(window);
    }

    g_ptr_array_unref(uris);
    return 0;
}

int
main(int argc, char *argv[])
{
    /* Let GTK4 and WebKit both use GPU — do NOT override GSK_RENDERER.
     * WebKitGTK works best with GTK's default renderer (gl/ngl).
     * Cairo forces software rendering = massive lag on modern web pages. */

    /* Disable WebKit's DMA-BUF renderer. On AMD/Intel iGPUs with Mesa
     * (and especially on X11) the DMA-BUF path causes web-content stutter
     * and flicker ("iGPU blink"). Disabling it keeps GPU acceleration via
     * the stable texture path and gives much smoother scrolling/rendering. */
    g_setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", FALSE);

    /* Single web process — reduces memory usage (~200MB less) */
    g_setenv("WEBKIT_USE_SINGLE_WEB_PROCESS", "1", FALSE);

    AdwApplication *app = adw_application_new(
        "io.github.ramm_fr.OpenBrowser",
        G_APPLICATION_HANDLES_OPEN | G_APPLICATION_HANDLES_COMMAND_LINE
    );

    /* Add command line options */
    GOptionEntry entries[] = {
        { "private", 'p', 0, G_OPTION_ARG_NONE, NULL, "Open in private mode", NULL },
        { "new-window", 'n', 0, G_OPTION_ARG_NONE, NULL, "Open new window", NULL },
        { NULL }
    };
    g_application_add_main_option_entries(G_APPLICATION(app), entries);

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "open", G_CALLBACK(on_open), NULL);
    g_signal_connect(app, "command-line", G_CALLBACK(handle_command_line), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
