#include "download-manager.h"
#include <string.h>

typedef struct {
    WebKitDownload *download;
    char *filename;
    char *destination;
    double progress;
    gboolean completed;
    gboolean cancelled;
    gboolean paused;
} DownloadItem;

struct _DownloadManager {
    GObject parent_instance;
    GPtrArray *downloads;
    GtkWidget *list_box;       /* sidebar widget */
    GtkWidget *live_list_box;  /* full-page downloads list */
    GtkWidget *empty_box;      /* empty state widget */
};

G_DEFINE_TYPE(DownloadManager, download_manager, G_TYPE_OBJECT)

static DownloadManager *default_instance = NULL;

/* Forward declarations */
static void on_live_progress_update(WebKitDownload *download, GParamSpec *pspec, gpointer user_data);
static void on_live_download_done(WebKitDownload *download, gpointer user_data);

static void
download_item_free(DownloadItem *item)
{
    if (item->download)
        g_object_unref(item->download);
    g_free(item->filename);
    g_free(item->destination);
    g_free(item);
}

static void
download_manager_dispose(GObject *object)
{
    DownloadManager *self = DOWNLOAD_MANAGER(object);
    g_clear_pointer(&self->downloads, g_ptr_array_unref);
    G_OBJECT_CLASS(download_manager_parent_class)->dispose(object);
}

static void
download_manager_class_init(DownloadManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = download_manager_dispose;
}

static void
download_manager_init(DownloadManager *self)
{
    self->downloads = g_ptr_array_new_with_free_func((GDestroyNotify)download_item_free);
    self->list_box = NULL;
    self->live_list_box = NULL;
    self->empty_box = NULL;
}

DownloadManager *
download_manager_get_default(void)
{
    if (!default_instance) {
        default_instance = g_object_new(DOWNLOAD_TYPE_MANAGER, NULL);
    }
    return default_instance;
}

static void
on_download_progress(WebKitDownload *download, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    DownloadItem *item = (DownloadItem *)user_data;
    item->progress = webkit_download_get_estimated_progress(download);

    /* Update live row if exists */
    GtkWidget *row = g_object_get_data(G_OBJECT(download), "live-row");
    if (row && ADW_IS_ACTION_ROW(row)) {
        const char *dest = webkit_download_get_destination(download);
        if (dest) {
            char *basename = g_path_get_basename(dest);
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), basename);
            g_free(basename);
        }
        char *subtitle = g_strdup_printf("Downloading... %.0f%%", item->progress * 100);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
        g_free(subtitle);
    }
}

static void
on_download_finished(WebKitDownload *download, gpointer user_data)
{
    (void)download;
    DownloadItem *item = (DownloadItem *)user_data;
    item->completed = TRUE;
    item->progress = 1.0;
}

static void
on_download_failed(WebKitDownload *download, GError *error, gpointer user_data)
{
    (void)download;
    (void)error;
    DownloadItem *item = (DownloadItem *)user_data;
    item->cancelled = TRUE;
}

static gboolean
on_download_decide_destination(WebKitDownload *download, const char *suggested_filename, gpointer user_data)
{
    (void)user_data;

    const char *download_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (!download_dir) {
        download_dir = g_get_home_dir();
    }

    char *destination = g_build_filename(download_dir, suggested_filename, NULL);

    /* If file exists, add a number */
    int counter = 1;
    char *final_dest = g_strdup(destination);
    while (g_file_test(final_dest, G_FILE_TEST_EXISTS)) {
        g_free(final_dest);
        char *name_without_ext = g_strdup(suggested_filename);
        char *ext = strrchr(name_without_ext, '.');
        if (ext) {
            *ext = '\0';
            ext++;
            final_dest = g_strdup_printf("%s/%s (%d).%s", download_dir, name_without_ext, counter, ext);
        } else {
            final_dest = g_strdup_printf("%s/%s (%d)", download_dir, name_without_ext, counter);
        }
        g_free(name_without_ext);
        counter++;
    }

    char *uri = g_filename_to_uri(final_dest, NULL, NULL);
    if (uri) {
        webkit_download_set_destination(download, uri);
        g_print("Download destination: %s\n", uri);
        g_free(uri);
    }

    g_free(destination);
    g_free(final_dest);
    return TRUE;
}

void
download_manager_add_download(DownloadManager *self, WebKitDownload *download)
{
    g_return_if_fail(DOWNLOAD_IS_MANAGER(self));

    DownloadItem *item = g_new0(DownloadItem, 1);
    item->download = g_object_ref(download);
    item->progress = 0.0;
    item->completed = FALSE;
    item->cancelled = FALSE;
    item->paused = FALSE;

    g_signal_connect(download, "notify::estimated-progress",
        G_CALLBACK(on_download_progress), item);
    g_signal_connect(download, "finished",
        G_CALLBACK(on_download_finished), item);
    g_signal_connect(download, "failed",
        G_CALLBACK(on_download_failed), item);

    g_ptr_array_add(self->downloads, item);

    /* If live downloads page is open, add the new download to it */
    if (self->live_list_box && gtk_widget_get_parent(self->live_list_box)) {
        /* Hide empty state if visible */
        if (self->empty_box) {
            gtk_widget_set_visible(self->empty_box, FALSE);
        }

        /* Create a row for this download */
        GtkWidget *row = adw_action_row_new();
            adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "Downloading...");
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), "Starting...");

        GtkWidget *icon = gtk_image_new_from_icon_name("folder-download-symbolic");
        adw_action_row_add_prefix(ADW_ACTION_ROW(row), icon);

        gtk_list_box_append(GTK_LIST_BOX(self->live_list_box), row);

        /* Add progress bar */
        GtkWidget *progress = gtk_progress_bar_new();
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), 0.0);
        gtk_widget_add_css_class(progress, "download-live-progress");
        gtk_widget_set_margin_start(progress, 16);
        gtk_widget_set_margin_end(progress, 16);
        gtk_widget_set_margin_bottom(progress, 8);

        g_signal_connect(download, "notify::estimated-progress",
            G_CALLBACK(on_live_progress_update), progress);
        g_signal_connect(download, "finished",
            G_CALLBACK(on_live_download_done), row);

        /* Update row title when destination is set */
        g_object_set_data(G_OBJECT(download), "live-row", row);

        gtk_list_box_append(GTK_LIST_BOX(self->live_list_box), progress);
    }
}

void
download_manager_pause(DownloadManager *self, int index)
{
    g_return_if_fail(DOWNLOAD_IS_MANAGER(self));
    g_return_if_fail(index >= 0 && index < (int)self->downloads->len);
    /* WebKitGTK doesn't have native pause, we just track state */
    DownloadItem *item = g_ptr_array_index(self->downloads, index);
    item->paused = TRUE;
}

void
download_manager_resume(DownloadManager *self, int index)
{
    g_return_if_fail(DOWNLOAD_IS_MANAGER(self));
    g_return_if_fail(index >= 0 && index < (int)self->downloads->len);
    DownloadItem *item = g_ptr_array_index(self->downloads, index);
    item->paused = FALSE;
}

void
download_manager_cancel(DownloadManager *self, int index)
{
    g_return_if_fail(DOWNLOAD_IS_MANAGER(self));
    g_return_if_fail(index >= 0 && index < (int)self->downloads->len);

    DownloadItem *item = g_ptr_array_index(self->downloads, index);
    webkit_download_cancel(item->download);
    item->cancelled = TRUE;
}

void
download_manager_retry(DownloadManager *self, int index)
{
    g_return_if_fail(DOWNLOAD_IS_MANAGER(self));
    g_return_if_fail(index >= 0 && index < (int)self->downloads->len);
    /* Retry by re-loading the URI - implementation depends on context */
}

void
download_manager_open_file(DownloadManager *self, int index)
{
    g_return_if_fail(DOWNLOAD_IS_MANAGER(self));
    g_return_if_fail(index >= 0 && index < (int)self->downloads->len);

    DownloadItem *item = g_ptr_array_index(self->downloads, index);
    if (item->completed) {
        const char *dest = webkit_download_get_destination(item->download);
        if (dest) {
            GFile *file = g_file_new_for_uri(dest);
            GAppInfo *app = g_app_info_get_default_for_type(
                g_content_type_guess(dest, NULL, 0, NULL), FALSE);
            if (app) {
                GList *files = g_list_append(NULL, file);
                g_app_info_launch(app, files, NULL, NULL);
                g_list_free(files);
                g_object_unref(app);
            }
            g_object_unref(file);
        }
    }
}

void
download_manager_open_folder(DownloadManager *self, int index)
{
    g_return_if_fail(DOWNLOAD_IS_MANAGER(self));
    g_return_if_fail(index >= 0 && index < (int)self->downloads->len);

    DownloadItem *item = g_ptr_array_index(self->downloads, index);
    const char *dest = webkit_download_get_destination(item->download);
    if (dest) {
        GFile *file = g_file_new_for_uri(dest);
        GFile *parent = g_file_get_parent(file);
        if (parent) {
            char *uri = g_file_get_uri(parent);
            g_app_info_launch_default_for_uri(uri, NULL, NULL);
            g_free(uri);
            g_object_unref(parent);
        }
        g_object_unref(file);
    }
}

void
download_manager_clear_completed(DownloadManager *self)
{
    g_return_if_fail(DOWNLOAD_IS_MANAGER(self));

    for (int i = self->downloads->len - 1; i >= 0; i--) {
        DownloadItem *item = g_ptr_array_index(self->downloads, i);
        if (item->completed || item->cancelled) {
            g_ptr_array_remove_index(self->downloads, i);
        }
    }
}

GListModel *
download_manager_get_downloads(DownloadManager *self)
{
    (void)self;
    return NULL; /* Simplified - would need GListStore */
}

GtkWidget *
download_manager_create_widget(DownloadManager *self)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 8);

    GtkWidget *title = gtk_label_new("Downloads");
    gtk_widget_add_css_class(title, "title-3");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_append(GTK_BOX(box), title);

    self->list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(self->list_box), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(self->list_box, "boxed-list");
    gtk_box_append(GTK_BOX(box), self->list_box);

    GtkWidget *placeholder = gtk_label_new("No downloads yet");
    gtk_widget_set_opacity(placeholder, 0.5);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(self->list_box), placeholder);

    /* Clear button */
    GtkWidget *clear_btn = gtk_button_new_with_label("Clear Completed");
    gtk_widget_add_css_class(clear_btn, "flat");
    gtk_widget_set_margin_top(clear_btn, 8);
    gtk_box_append(GTK_BOX(box), clear_btn);

    return box;
}

static void
on_live_progress_update(WebKitDownload *download, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    GtkProgressBar *progress = GTK_PROGRESS_BAR(user_data);
    double fraction = webkit_download_get_estimated_progress(download);
    gtk_progress_bar_set_fraction(progress, fraction);
}

static void
on_live_download_done(WebKitDownload *download, gpointer user_data)
{
    (void)download;
    GtkWidget *row = GTK_WIDGET(user_data);
    if (ADW_IS_ACTION_ROW(row)) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), "Completed");
    }
}

static void
on_open_downloads_folder(GtkButton *button, gpointer user_data)
{
    (void)button;
    (void)user_data;
    const char *download_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (!download_dir) download_dir = g_get_home_dir();

    char *uri = g_filename_to_uri(download_dir, NULL, NULL);
    if (uri) {
        g_app_info_launch_default_for_uri(uri, NULL, NULL);
        g_free(uri);
    }
}

static void
on_download_delete_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    const char *path = g_object_get_data(G_OBJECT(button), "download-path");
    if (path && *path) {
        /* Delete the file */
        GFile *file = g_file_new_for_path(path);
        GError *error = NULL;
        if (g_file_delete(file, NULL, &error)) {
            g_print("[OpenBrowser] Deleted: %s\n", path);
        } else {
            g_print("[OpenBrowser] Delete failed: %s\n", error ? error->message : "unknown");
            if (error) g_error_free(error);
        }
        g_object_unref(file);

        /* Hide the row */
        GtkWidget *row = gtk_widget_get_parent(GTK_WIDGET(button));
        while (row && !ADW_IS_ACTION_ROW(row)) {
            row = gtk_widget_get_parent(row);
        }
        if (row) {
            gtk_widget_set_visible(row, FALSE);
        }
    }
}

static void
on_download_folder_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    const char *path = g_object_get_data(G_OBJECT(button), "download-path");
    if (path && *path) {
        char *dir = g_path_get_dirname(path);
        char *uri = g_filename_to_uri(dir, NULL, NULL);
        if (uri) {
            g_app_info_launch_default_for_uri(uri, NULL, NULL);
            g_free(uri);
        }
        g_free(dir);
    }
}

GtkWidget *
download_manager_create_full_page(DownloadManager *self)
{
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);

    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(page, 40);
    gtk_widget_set_margin_end(page, 40);
    gtk_widget_set_margin_top(page, 32);
    gtk_widget_set_margin_bottom(page, 32);
    gtk_widget_set_halign(page, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(page, 600, -1);

    /* Header */
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *header_icon = gtk_image_new_from_icon_name("folder-download-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(header_icon), 32);
    gtk_box_append(GTK_BOX(header_box), header_icon);

    GtkWidget *header_text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title = gtk_label_new("Downloads");
    gtk_widget_add_css_class(title, "title-1");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_append(GTK_BOX(header_text_box), title);

    GtkWidget *subtitle = gtk_label_new("Your downloaded files appear here");
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0);
    gtk_box_append(GTK_BOX(header_text_box), subtitle);

    gtk_box_append(GTK_BOX(header_box), header_text_box);
    gtk_box_append(GTK_BOX(page), header_box);

    /* Separator */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(page), sep);

    /* Downloads list */
    GtkWidget *list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(list_box, "boxed-list");

    /* Store reference for live updates */
    self->live_list_box = list_box;

    if (self->downloads->len == 0) {
        /* Empty state */
        GtkWidget *empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        self->empty_box = empty_box;
        gtk_widget_set_margin_top(empty_box, 60);
        gtk_widget_set_margin_bottom(empty_box, 60);
        gtk_widget_set_halign(empty_box, GTK_ALIGN_CENTER);

        GtkWidget *empty_icon = gtk_image_new_from_icon_name("folder-download-symbolic");
        gtk_image_set_pixel_size(GTK_IMAGE(empty_icon), 64);
        gtk_widget_set_opacity(empty_icon, 0.3);
        gtk_box_append(GTK_BOX(empty_box), empty_icon);

        GtkWidget *empty_title = gtk_label_new("No downloads yet");
        gtk_widget_add_css_class(empty_title, "title-3");
        gtk_widget_set_opacity(empty_title, 0.5);
        gtk_box_append(GTK_BOX(empty_box), empty_title);

        GtkWidget *empty_desc = gtk_label_new("Files you download will appear here");
        gtk_widget_set_opacity(empty_desc, 0.4);
        gtk_box_append(GTK_BOX(empty_box), empty_desc);

        gtk_box_append(GTK_BOX(page), empty_box);
    } else {
        for (guint i = 0; i < self->downloads->len; i++) {
            DownloadItem *item = g_ptr_array_index(self->downloads, i);

            GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            gtk_widget_add_css_class(row_box, "download-row");

            GtkWidget *row = adw_action_row_new();
            adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
            const char *dest = webkit_download_get_destination(item->download);
            if (dest) {
                char *basename = g_path_get_basename(dest);
                adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), basename);
                g_free(basename);
            } else {
                adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "Downloading...");
            }

            if (item->completed) {
                adw_action_row_set_subtitle(ADW_ACTION_ROW(row), "Completed");
            } else if (item->cancelled) {
                adw_action_row_set_subtitle(ADW_ACTION_ROW(row), "Cancelled");
            } else {
                char *progress_text = g_strdup_printf("Downloading... %.0f%%", item->progress * 100);
                adw_action_row_set_subtitle(ADW_ACTION_ROW(row), progress_text);
                g_free(progress_text);
            }

            /* Icon - different for completed vs active */
            const char *icon_name = item->completed ? "emblem-ok-symbolic" :
                                    item->cancelled ? "dialog-error-symbolic" :
                                    "folder-download-symbolic";
            GtkWidget *icon = gtk_image_new_from_icon_name(icon_name);
            adw_action_row_add_prefix(ADW_ACTION_ROW(row), icon);

            /* Delete button */
            GtkWidget *delete_btn = gtk_button_new_from_icon_name("user-trash-symbolic");
            gtk_widget_add_css_class(delete_btn, "flat");
            gtk_widget_add_css_class(delete_btn, "circular");
            gtk_widget_set_valign(delete_btn, GTK_ALIGN_CENTER);
            gtk_widget_set_tooltip_text(delete_btn, "Delete file");
            g_object_set_data(G_OBJECT(delete_btn), "download-path",
                (gpointer)(dest ? dest : ""));
            g_signal_connect(delete_btn, "clicked",
                G_CALLBACK(on_download_delete_clicked), self);
            adw_action_row_add_suffix(ADW_ACTION_ROW(row), delete_btn);

            /* Open folder button */
            GtkWidget *folder_btn = gtk_button_new_from_icon_name("folder-open-symbolic");
            gtk_widget_add_css_class(folder_btn, "flat");
            gtk_widget_add_css_class(folder_btn, "circular");
            gtk_widget_set_valign(folder_btn, GTK_ALIGN_CENTER);
            gtk_widget_set_tooltip_text(folder_btn, "Open folder");
            g_object_set_data(G_OBJECT(folder_btn), "download-path",
                (gpointer)(dest ? dest : ""));
            g_signal_connect(folder_btn, "clicked",
                G_CALLBACK(on_download_folder_clicked), NULL);
            adw_action_row_add_suffix(ADW_ACTION_ROW(row), folder_btn);

            gtk_list_box_append(GTK_LIST_BOX(list_box), row);

            /* Live progress bar for active downloads */
            if (!item->completed && !item->cancelled) {
                GtkWidget *progress = gtk_progress_bar_new();
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), item->progress);
                gtk_widget_add_css_class(progress, "download-live-progress");
                gtk_widget_set_margin_start(progress, 16);
                gtk_widget_set_margin_end(progress, 16);
                gtk_widget_set_margin_bottom(progress, 8);

                /* Connect to real-time progress updates */
                g_signal_connect(item->download, "notify::estimated-progress",
                    G_CALLBACK(on_live_progress_update), progress);
                g_signal_connect(item->download, "finished",
                    G_CALLBACK(on_live_download_done), row);

                gtk_list_box_append(GTK_LIST_BOX(list_box), progress);
            }
        }
        gtk_box_append(GTK_BOX(page), list_box);
    }

    /* Info section */
    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(info_box, 16);

    GtkWidget *info_icon = gtk_image_new_from_icon_name("folder-symbolic");
    gtk_widget_set_opacity(info_icon, 0.5);
    gtk_box_append(GTK_BOX(info_box), info_icon);

    char *dl_path = g_strdup_printf("Download location: %s",
        g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD));
    GtkWidget *info_label = gtk_label_new(dl_path);
    gtk_widget_set_opacity(info_label, 0.5);
    gtk_label_set_xalign(GTK_LABEL(info_label), 0);
    gtk_widget_set_hexpand(info_label, TRUE);
    gtk_box_append(GTK_BOX(info_box), info_label);
    g_free(dl_path);

    /* Open folder button */
    GtkWidget *open_folder_btn = gtk_button_new_with_label("Open Folder");
    gtk_widget_add_css_class(open_folder_btn, "flat");
    g_signal_connect(open_folder_btn, "clicked",
        G_CALLBACK(on_open_downloads_folder), NULL);
    gtk_box_append(GTK_BOX(info_box), open_folder_btn);

    gtk_box_append(GTK_BOX(page), info_box);

    /* Stats section */
    GtkWidget *stats_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_margin_top(stats_box, 20);
    gtk_widget_set_halign(stats_box, GTK_ALIGN_CENTER);

    int completed = 0, active = 0, cancelled = 0;
    for (guint i = 0; i < self->downloads->len; i++) {
        DownloadItem *item = g_ptr_array_index(self->downloads, i);
        if (item->completed) completed++;
        else if (item->cancelled) cancelled++;
        else active++;
    }

    char *stats_text = g_strdup_printf(
        "Total: %u  |  Completed: %d  |  Active: %d  |  Cancelled: %d",
        self->downloads->len, completed, active, cancelled);
    GtkWidget *stats_label = gtk_label_new(stats_text);
    gtk_widget_set_opacity(stats_label, 0.4);
    gtk_widget_add_css_class(stats_label, "caption");
    gtk_box_append(GTK_BOX(stats_box), stats_label);
    g_free(stats_text);

    gtk_box_append(GTK_BOX(page), stats_box);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), page);
    return scrolled;
}
