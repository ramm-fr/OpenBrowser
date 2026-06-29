#include "bookmark-manager.h"
#include "browser-window.h"

struct _BookmarkManager {
    GObject parent_instance;
    GList *bookmarks;
    GList *folders;
    char *data_file;
};

G_DEFINE_TYPE(BookmarkManager, bookmark_manager, G_TYPE_OBJECT)

static BookmarkManager *default_instance = NULL;

static void
bookmark_item_free(BookmarkItem *item)
{
    g_free(item->title);
    g_free(item->uri);
    g_free(item->folder);
    g_free(item);
}

static BookmarkItem *
bookmark_item_new(const char *title, const char *uri, const char *folder)
{
    BookmarkItem *item = g_new0(BookmarkItem, 1);
    item->title = g_strdup(title);
    item->uri = g_strdup(uri);
    item->folder = g_strdup(folder ? folder : "Unsorted");
    item->timestamp = g_get_real_time() / G_USEC_PER_SEC;
    return item;
}

static void
bookmark_manager_load(BookmarkManager *self)
{
    if (!g_file_test(self->data_file, G_FILE_TEST_EXISTS))
        return;

    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    if (!json_parser_load_from_file(parser, self->data_file, &error)) {
        g_warning("Failed to load bookmarks: %s", error->message);
        g_error_free(error);
        g_object_unref(parser);
        return;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonArray *array = json_node_get_array(root);

    for (guint i = 0; i < json_array_get_length(array); i++) {
        JsonObject *obj = json_array_get_object_element(array, i);
        BookmarkItem *item = bookmark_item_new(
            json_object_get_string_member(obj, "title"),
            json_object_get_string_member(obj, "uri"),
            json_object_get_string_member(obj, "folder")
        );
        item->timestamp = json_object_get_int_member(obj, "timestamp");
        self->bookmarks = g_list_append(self->bookmarks, item);
    }

    g_object_unref(parser);
}

static void
bookmark_manager_save(BookmarkManager *self)
{
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_array(builder);

    for (GList *l = self->bookmarks; l; l = l->next) {
        BookmarkItem *item = l->data;
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "title");
        json_builder_add_string_value(builder, item->title);
        json_builder_set_member_name(builder, "uri");
        json_builder_add_string_value(builder, item->uri);
        json_builder_set_member_name(builder, "folder");
        json_builder_add_string_value(builder, item->folder);
        json_builder_set_member_name(builder, "timestamp");
        json_builder_add_int_value(builder, item->timestamp);
        json_builder_end_object(builder);
    }

    json_builder_end_array(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_pretty(gen, TRUE);
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);

    /* Ensure directory exists */
    char *dir = g_path_get_dirname(self->data_file);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    GError *error = NULL;
    if (!json_generator_to_file(gen, self->data_file, &error)) {
        g_warning("Failed to save bookmarks: %s", error->message);
        g_error_free(error);
    }

    json_node_unref(root);
    g_object_unref(gen);
    g_object_unref(builder);
}

static void
bookmark_manager_dispose(GObject *object)
{
    BookmarkManager *self = BOOKMARK_MANAGER(object);
    bookmark_manager_save(self);
    g_list_free_full(self->bookmarks, (GDestroyNotify)bookmark_item_free);
    self->bookmarks = NULL;
    g_list_free_full(self->folders, g_free);
    self->folders = NULL;
    g_free(self->data_file);
    G_OBJECT_CLASS(bookmark_manager_parent_class)->dispose(object);
}

static void
bookmark_manager_class_init(BookmarkManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = bookmark_manager_dispose;
}

static void
bookmark_manager_init(BookmarkManager *self)
{
    self->bookmarks = NULL;
    self->folders = NULL;
    self->data_file = g_build_filename(g_get_user_data_dir(), "openbrowser", "bookmarks.json", NULL);

    /* Default folders */
    self->folders = g_list_append(self->folders, g_strdup("Unsorted"));
    self->folders = g_list_append(self->folders, g_strdup("Favorites"));
    self->folders = g_list_append(self->folders, g_strdup("Reading List"));

    bookmark_manager_load(self);
}

BookmarkManager *
bookmark_manager_get_default(void)
{
    if (!default_instance) {
        default_instance = g_object_new(BOOKMARK_TYPE_MANAGER, NULL);
    }
    return default_instance;
}

void
bookmark_manager_add(BookmarkManager *self, const char *title, const char *uri, const char *folder)
{
    g_return_if_fail(BOOKMARK_IS_MANAGER(self));
    g_return_if_fail(uri != NULL);

    if (bookmark_manager_exists(self, uri))
        return;

    BookmarkItem *item = bookmark_item_new(title ? title : uri, uri, folder);
    self->bookmarks = g_list_append(self->bookmarks, item);
    bookmark_manager_save(self);
}

void
bookmark_manager_remove(BookmarkManager *self, const char *uri)
{
    g_return_if_fail(BOOKMARK_IS_MANAGER(self));

    for (GList *l = self->bookmarks; l; l = l->next) {
        BookmarkItem *item = l->data;
        if (g_strcmp0(item->uri, uri) == 0) {
            self->bookmarks = g_list_delete_link(self->bookmarks, l);
            bookmark_item_free(item);
            bookmark_manager_save(self);
            return;
        }
    }
}

gboolean
bookmark_manager_exists(BookmarkManager *self, const char *uri)
{
    g_return_val_if_fail(BOOKMARK_IS_MANAGER(self), FALSE);

    for (GList *l = self->bookmarks; l; l = l->next) {
        BookmarkItem *item = l->data;
        if (g_strcmp0(item->uri, uri) == 0)
            return TRUE;
    }
    return FALSE;
}

GList *
bookmark_manager_get_all(BookmarkManager *self)
{
    g_return_val_if_fail(BOOKMARK_IS_MANAGER(self), NULL);
    return self->bookmarks;
}

GList *
bookmark_manager_get_folder(BookmarkManager *self, const char *folder)
{
    g_return_val_if_fail(BOOKMARK_IS_MANAGER(self), NULL);

    GList *result = NULL;
    for (GList *l = self->bookmarks; l; l = l->next) {
        BookmarkItem *item = l->data;
        if (g_strcmp0(item->folder, folder) == 0) {
            result = g_list_append(result, item);
        }
    }
    return result;
}

GList *
bookmark_manager_search(BookmarkManager *self, const char *query)
{
    g_return_val_if_fail(BOOKMARK_IS_MANAGER(self), NULL);

    GList *result = NULL;
    char *lower_query = g_utf8_strdown(query, -1);

    for (GList *l = self->bookmarks; l; l = l->next) {
        BookmarkItem *item = l->data;
        char *lower_title = g_utf8_strdown(item->title, -1);
        char *lower_uri = g_utf8_strdown(item->uri, -1);

        if (g_strstr_len(lower_title, -1, lower_query) ||
            g_strstr_len(lower_uri, -1, lower_query)) {
            result = g_list_append(result, item);
        }

        g_free(lower_title);
        g_free(lower_uri);
    }

    g_free(lower_query);
    return result;
}

void
bookmark_manager_add_folder(BookmarkManager *self, const char *name)
{
    g_return_if_fail(BOOKMARK_IS_MANAGER(self));
    self->folders = g_list_append(self->folders, g_strdup(name));
}

void
bookmark_manager_remove_folder(BookmarkManager *self, const char *name)
{
    g_return_if_fail(BOOKMARK_IS_MANAGER(self));

    for (GList *l = self->folders; l; l = l->next) {
        if (g_strcmp0(l->data, name) == 0) {
            g_free(l->data);
            self->folders = g_list_delete_link(self->folders, l);
            return;
        }
    }
}

GList *
bookmark_manager_get_folders(BookmarkManager *self)
{
    g_return_val_if_fail(BOOKMARK_IS_MANAGER(self), NULL);
    return self->folders;
}

void
bookmark_manager_export_html(BookmarkManager *self, const char *path)
{
    g_return_if_fail(BOOKMARK_IS_MANAGER(self));

    GString *html = g_string_new("<!DOCTYPE NETSCAPE-Bookmark-file-1>\n");
    g_string_append(html, "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n");
    g_string_append(html, "<TITLE>Bookmarks</TITLE>\n<H1>Bookmarks</H1>\n<DL><p>\n");

    for (GList *l = self->bookmarks; l; l = l->next) {
        BookmarkItem *item = l->data;
        g_string_append_printf(html, "    <DT><A HREF=\"%s\">%s</A>\n", item->uri, item->title);
    }

    g_string_append(html, "</DL><p>\n");

    GError *error = NULL;
    g_file_set_contents(path, html->str, html->len, &error);
    if (error) {
        g_warning("Export failed: %s", error->message);
        g_error_free(error);
    }
    g_string_free(html, TRUE);
}

void
bookmark_manager_import_html(BookmarkManager *self, const char *path)
{
    g_return_if_fail(BOOKMARK_IS_MANAGER(self));

    char *content = NULL;
    GError *error = NULL;

    if (!g_file_get_contents(path, &content, NULL, &error)) {
        g_warning("Import failed: %s", error->message);
        g_error_free(error);
        return;
    }

    /* Simple HTML bookmark parser */
    char *pos = content;
    while ((pos = g_strstr_len(pos, -1, "HREF=\"")) != NULL) {
        pos += 6;
        char *end = g_strstr_len(pos, -1, "\"");
        if (!end) break;

        char *uri = g_strndup(pos, end - pos);
        pos = end + 2; /* Skip "> */

        char *title_end = g_strstr_len(pos, -1, "</A>");
        char *title = title_end ? g_strndup(pos, title_end - pos) : g_strdup(uri);

        bookmark_manager_add(self, title, uri, "Imported");

        g_free(uri);
        g_free(title);
        if (title_end) pos = title_end;
    }

    g_free(content);
}

GtkWidget *
bookmark_manager_create_widget(BookmarkManager *self)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 8);

    GtkWidget *title = gtk_label_new("Bookmarks");
    gtk_widget_add_css_class(title, "title-3");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_append(GTK_BOX(box), title);

    /* Search entry */
    GtkWidget *search = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(search), "Search bookmarks...");
    gtk_box_append(GTK_BOX(box), search);

    /* Bookmarks list */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);

    GtkWidget *list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box), GTK_SELECTION_NONE);

    for (GList *l = self->bookmarks; l; l = l->next) {
        BookmarkItem *item = l->data;

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_add_css_class(row, "sidebar-item");

        GtkWidget *icon = gtk_image_new_from_icon_name("starred-symbolic");
        gtk_box_append(GTK_BOX(row), icon);

        GtkWidget *label = gtk_label_new(item->title);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_widget_set_hexpand(label, TRUE);
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_box_append(GTK_BOX(row), label);

        gtk_list_box_append(GTK_LIST_BOX(list_box), row);
    }

    GtkWidget *placeholder = gtk_label_new("No bookmarks yet");
    gtk_widget_set_opacity(placeholder, 0.5);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(list_box), placeholder);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list_box);
    gtk_box_append(GTK_BOX(box), scrolled);

    return box;
}

GtkWidget *
bookmark_manager_create_bar(BookmarkManager *self)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(bar, "bookmark-bar");

    for (GList *l = self->bookmarks; l; l = l->next) {
        BookmarkItem *item = l->data;

        GtkWidget *btn = gtk_button_new_with_label(item->title);
        gtk_widget_add_css_class(btn, "bookmark-button");
        gtk_widget_add_css_class(btn, "flat");
        gtk_box_append(GTK_BOX(bar), btn);
    }

    return bar;
}

static void
on_bookmark_open_clicked(GtkButton *button, gpointer user_data)
{
    BookmarkManager *self = BOOKMARK_MANAGER(user_data);
    (void)self;
    const char *uri = g_object_get_data(G_OBJECT(button), "uri");
    if (uri && *uri) {
        /* Find the browser window and navigate in a new tab */
        GtkWidget *widget = GTK_WIDGET(button);
        GtkRoot *root = gtk_widget_get_root(widget);
        if (root && BROWSER_IS_WINDOW(root)) {
            browser_window_new_tab(BROWSER_WINDOW(root), uri);
        }
    }
}

static void
on_bookmark_delete_clicked(GtkButton *button, gpointer user_data)
{
    BookmarkManager *self = BOOKMARK_MANAGER(user_data);
    const char *uri = g_object_get_data(G_OBJECT(button), "uri");
    GtkWidget *row = g_object_get_data(G_OBJECT(button), "row-widget");

    if (uri && *uri) {
        bookmark_manager_remove(self, uri);
        if (row) {
            gtk_widget_set_visible(row, FALSE);
        }
    }
}

GtkWidget *
bookmark_manager_create_full_page(BookmarkManager *self)
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
    GtkWidget *header_icon = gtk_image_new_from_icon_name("starred-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(header_icon), 32);
    gtk_box_append(GTK_BOX(header_box), header_icon);

    GtkWidget *header_text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title = gtk_label_new("Bookmarks");
    gtk_widget_add_css_class(title, "title-1");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_append(GTK_BOX(header_text_box), title);

    GtkWidget *subtitle = gtk_label_new("Your saved pages");
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0);
    gtk_box_append(GTK_BOX(header_text_box), subtitle);

    gtk_widget_set_hexpand(header_text_box, TRUE);
    gtk_box_append(GTK_BOX(header_box), header_text_box);
    gtk_box_append(GTK_BOX(page), header_box);

    /* Separator */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(page), sep);

    /* Bookmarks list */
    GList *all_bookmarks = bookmark_manager_get_all(self);

    if (!all_bookmarks) {
        /* Empty state */
        GtkWidget *empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margin_top(empty_box, 60);
        gtk_widget_set_margin_bottom(empty_box, 60);
        gtk_widget_set_halign(empty_box, GTK_ALIGN_CENTER);

        GtkWidget *empty_icon = gtk_image_new_from_icon_name("starred-symbolic");
        gtk_image_set_pixel_size(GTK_IMAGE(empty_icon), 64);
        gtk_widget_set_opacity(empty_icon, 0.3);
        gtk_box_append(GTK_BOX(empty_box), empty_icon);

        GtkWidget *empty_title = gtk_label_new("No bookmarks yet");
        gtk_widget_add_css_class(empty_title, "title-3");
        gtk_widget_set_opacity(empty_title, 0.5);
        gtk_box_append(GTK_BOX(empty_box), empty_title);

        GtkWidget *empty_desc = gtk_label_new("Press Ctrl+D to bookmark a page");
        gtk_widget_set_opacity(empty_desc, 0.4);
        gtk_box_append(GTK_BOX(empty_box), empty_desc);

        gtk_box_append(GTK_BOX(page), empty_box);
    } else {
        /* List of bookmarks */
        GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

        for (GList *l = all_bookmarks; l; l = l->next) {
            BookmarkItem *item = l->data;
            if (!item->title || !item->uri) continue;

            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_widget_add_css_class(row, "history-row");

            GtkWidget *icon = gtk_image_new_from_icon_name("starred-symbolic");
            gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
            gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
            gtk_box_append(GTK_BOX(row), icon);

            GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
            gtk_widget_set_hexpand(text_box, TRUE);

            GtkWidget *title_label = gtk_label_new(item->title);
            gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
            gtk_label_set_xalign(GTK_LABEL(title_label), 0);
            gtk_box_append(GTK_BOX(text_box), title_label);

            GtkWidget *uri_label = gtk_label_new(item->uri);
            gtk_label_set_ellipsize(GTK_LABEL(uri_label), PANGO_ELLIPSIZE_END);
            gtk_label_set_xalign(GTK_LABEL(uri_label), 0);
            gtk_widget_set_opacity(uri_label, 0.5);
            gtk_widget_add_css_class(uri_label, "caption");
            gtk_box_append(GTK_BOX(text_box), uri_label);

            gtk_box_append(GTK_BOX(row), text_box);

            GtkWidget *folder_label = gtk_label_new(item->folder);
            gtk_widget_set_opacity(folder_label, 0.5);
            gtk_widget_set_valign(folder_label, GTK_ALIGN_CENTER);
            gtk_box_append(GTK_BOX(row), folder_label);

            /* Open button */
            GtkWidget *open_btn = gtk_button_new_from_icon_name("go-next-symbolic");
            gtk_widget_add_css_class(open_btn, "flat");
            gtk_widget_add_css_class(open_btn, "circular");
            gtk_widget_set_valign(open_btn, GTK_ALIGN_CENTER);
            gtk_widget_set_tooltip_text(open_btn, "Open");
            g_object_set_data_full(G_OBJECT(open_btn), "uri", g_strdup(item->uri), g_free);
            g_signal_connect(open_btn, "clicked", G_CALLBACK(on_bookmark_open_clicked), self);
            gtk_box_append(GTK_BOX(row), open_btn);

            /* Delete button */
            GtkWidget *del_btn = gtk_button_new_from_icon_name("user-trash-symbolic");
            gtk_widget_add_css_class(del_btn, "flat");
            gtk_widget_add_css_class(del_btn, "circular");
            gtk_widget_set_valign(del_btn, GTK_ALIGN_CENTER);
            gtk_widget_set_tooltip_text(del_btn, "Delete");
            g_object_set_data_full(G_OBJECT(del_btn), "uri", g_strdup(item->uri), g_free);
            g_object_set_data(G_OBJECT(del_btn), "row-widget", row);
            g_signal_connect(del_btn, "clicked", G_CALLBACK(on_bookmark_delete_clicked), self);
            gtk_box_append(GTK_BOX(row), del_btn);

            gtk_box_append(GTK_BOX(list_box), row);
        }

        gtk_box_append(GTK_BOX(page), list_box);

        /* Count */
        int count = g_list_length(all_bookmarks);
        char *count_text = g_strdup_printf("%d bookmark%s", count, count == 1 ? "" : "s");
        GtkWidget *count_label = gtk_label_new(count_text);
        gtk_widget_set_opacity(count_label, 0.4);
        gtk_widget_set_margin_top(count_label, 12);
        gtk_widget_set_halign(count_label, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(page), count_label);
        g_free(count_text);
    }

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), page);
    return scrolled;
}
