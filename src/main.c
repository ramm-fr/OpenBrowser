#include <adwaita.h>
#include <webkit/webkit.h>
#include "browser-window.h"
#include "settings-manager.h"

static void
on_activate(AdwApplication *app, gpointer user_data)
{
    (void)user_data;

    /* Load CSS */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/com/openbrowser/style.css");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    /* Apply dark theme */
    AdwStyleManager *style_mgr = adw_style_manager_get_default();
    adw_style_manager_set_color_scheme(style_mgr, ADW_COLOR_SCHEME_FORCE_DARK);

    /* Create main window */
    BrowserWindow *window = browser_window_new(app);

    /* Set window icon */
    gtk_window_set_icon_name(GTK_WINDOW(window), "com.openbrowser");

    gtk_window_present(GTK_WINDOW(window));
}

static void
on_open(GApplication *app, GFile **files, int n_files, const char *hint, gpointer user_data)
{
    (void)hint;
    (void)user_data;

    on_activate(ADW_APPLICATION(app), NULL);

    GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(app));
    if (window && n_files > 0) {
        for (int i = 0; i < n_files; i++) {
            char *uri = g_file_get_uri(files[i]);
            browser_window_new_tab(BROWSER_WINDOW(window), uri);
            g_free(uri);
        }
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

    g_application_activate(app);
    return 0;
}

int
main(int argc, char *argv[])
{
    AdwApplication *app = adw_application_new(
        "com.openbrowser.app",
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
