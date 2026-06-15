#include "history-manager.h"

struct _HistoryManager {
    GObject parent_instance;
    GList *entries;
    char *data_file;
};

G_DEFINE_TYPE(HistoryManager, history_manager, G_TYPE_OBJECT)

static HistoryManager *default_instance = NULL;

static void
history_entry_free(HistoryEntry *entry)
{
    g_free(entry->title);
    g_free(entry->uri);
    g_free(entry);
}

static HistoryEntry *
history_entry_new(const char *title, const char *uri)
{
    HistoryEntry *entry = g_new0(HistoryEntry, 1);
    entry->title = g_strdup(title);
    entry->uri = g_strdup(uri);
    entry->visit_time = g_get_real_time() / G_USEC_PER_SEC;
    entry->visit_count = 1;
    return entry;
}

static void
history_manager_load(HistoryManager *self)
{
    if (!g_file_test(self->data_file, G_FILE_TEST_EXISTS))
        return;

    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    if (!json_parser_load_from_file(parser, self->data_file, &error)) {
        g_warning("Failed to load history: %s", error->message);
        g_error_free(error);
        g_object_unref(parser);
        return;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonArray *array = json_node_get_array(root);

    for (guint i = 0; i < json_array_get_length(array); i++) {
        JsonObject *obj = json_array_get_object_element(array, i);
        HistoryEntry *entry = history_entry_new(
            json_object_get_string_member(obj, "title"),
            json_object_get_string_member(obj, "uri")
        );
        entry->visit_time = json_object_get_int_member(obj, "visit_time");
        entry->visit_count = json_object_get_int_member(obj, "visit_count");
        self->entries = g_list_append(self->entries, entry);
    }

    g_object_unref(parser);
}

static void
history_manager_save_to_file(HistoryManager *self)
{
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_array(builder);

    for (GList *l = self->entries; l; l = l->next) {
        HistoryEntry *entry = l->data;
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "title");
        json_builder_add_string_value(builder, entry->title);
        json_builder_set_member_name(builder, "uri");
        json_builder_add_string_value(builder, entry->uri);
        json_builder_set_member_name(builder, "visit_time");
        json_builder_add_int_value(builder, entry->visit_time);
        json_builder_set_member_name(builder, "visit_count");
        json_builder_add_int_value(builder, entry->visit_count);
        json_builder_end_object(builder);
    }

    json_builder_end_array(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_pretty(gen, TRUE);
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);

    char *dir = g_path_get_dirname(self->data_file);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    GError *error = NULL;
    if (!json_generator_to_file(gen, self->data_file, &error)) {
        g_warning("Failed to save history: %s", error->message);
        g_error_free(error);
    }

    json_node_unref(root);
    g_object_unref(gen);
    g_object_unref(builder);
}

static void
history_manager_dispose(GObject *object)
{
    HistoryManager *self = HISTORY_MANAGER(object);
    history_manager_save_to_file(self);
    g_list_free_full(self->entries, (GDestroyNotify)history_entry_free);
    self->entries = NULL;
    g_free(self->data_file);
    G_OBJECT_CLASS(history_manager_parent_class)->dispose(object);
}

static void
history_manager_class_init(HistoryManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = history_manager_dispose;
}

static void
history_manager_init(HistoryManager *self)
{
    self->entries = NULL;
    self->data_file = g_build_filename(g_get_user_data_dir(), "openbrowser", "history.json", NULL);
    history_manager_load(self);
}

HistoryManager *
history_manager_get_default(void)
{
    if (!default_instance) {
        default_instance = g_object_new(HISTORY_TYPE_MANAGER, NULL);
    }
    return default_instance;
}

void
history_manager_add(HistoryManager *self, const char *title, const char *uri)
{
    g_return_if_fail(HISTORY_IS_MANAGER(self));
    g_return_if_fail(uri != NULL);

    /* Skip internal pages */
    if (g_str_has_prefix(uri, "about:"))
        return;
    if (g_strcmp0(uri, "https://unpkg.com") == 0 || g_strcmp0(uri, "https://unpkg.com/") == 0)
        return;

    /* Check if entry already exists */
    for (GList *l = self->entries; l; l = l->next) {
        HistoryEntry *entry = l->data;
        if (g_strcmp0(entry->uri, uri) == 0) {
            entry->visit_count++;
            entry->visit_time = g_get_real_time() / G_USEC_PER_SEC;
            g_free(entry->title);
            entry->title = g_strdup(title ? title : uri);
            /* Move to front */
            self->entries = g_list_delete_link(self->entries, l);
            self->entries = g_list_prepend(self->entries, entry);
            history_manager_save_to_file(self);
            return;
        }
    }

    HistoryEntry *entry = history_entry_new(title ? title : uri, uri);
    self->entries = g_list_prepend(self->entries, entry);
    history_manager_save_to_file(self);
}

void
history_manager_remove(HistoryManager *self, const char *uri)
{
    g_return_if_fail(HISTORY_IS_MANAGER(self));

    for (GList *l = self->entries; l; l = l->next) {
        HistoryEntry *entry = l->data;
        if (g_strcmp0(entry->uri, uri) == 0) {
            self->entries = g_list_delete_link(self->entries, l);
            history_entry_free(entry);
            history_manager_save_to_file(self);
            return;
        }
    }
}

void
history_manager_clear(HistoryManager *self)
{
    g_return_if_fail(HISTORY_IS_MANAGER(self));
    g_list_free_full(self->entries, (GDestroyNotify)history_entry_free);
    self->entries = NULL;
    history_manager_save_to_file(self);
}

void
history_manager_clear_range(HistoryManager *self, gint64 start, gint64 end)
{
    g_return_if_fail(HISTORY_IS_MANAGER(self));

    GList *l = self->entries;
    while (l) {
        GList *next = l->next;
        HistoryEntry *entry = l->data;
        if (entry->visit_time >= start && entry->visit_time <= end) {
            self->entries = g_list_delete_link(self->entries, l);
            history_entry_free(entry);
        }
        l = next;
    }
    history_manager_save_to_file(self);
}

GList *
history_manager_get_all(HistoryManager *self)
{
    g_return_val_if_fail(HISTORY_IS_MANAGER(self), NULL);
    return self->entries;
}

GList *
history_manager_get_recent(HistoryManager *self, int count)
{
    g_return_val_if_fail(HISTORY_IS_MANAGER(self), NULL);

    GList *result = NULL;
    int n = 0;
    for (GList *l = self->entries; l && n < count; l = l->next, n++) {
        result = g_list_append(result, l->data);
    }
    return result;
}

GList *
history_manager_search(HistoryManager *self, const char *query)
{
    g_return_val_if_fail(HISTORY_IS_MANAGER(self), NULL);

    GList *result = NULL;
    char *lower_query = g_utf8_strdown(query, -1);

    for (GList *l = self->entries; l; l = l->next) {
        HistoryEntry *entry = l->data;
        char *lower_title = g_utf8_strdown(entry->title, -1);
        char *lower_uri = g_utf8_strdown(entry->uri, -1);

        if (g_strstr_len(lower_title, -1, lower_query) ||
            g_strstr_len(lower_uri, -1, lower_query)) {
            result = g_list_append(result, entry);
        }

        g_free(lower_title);
        g_free(lower_uri);
    }

    g_free(lower_query);
    return result;
}

static gint
compare_visit_count(gconstpointer a, gconstpointer b)
{
    const HistoryEntry *ea = a;
    const HistoryEntry *eb = b;
    return eb->visit_count - ea->visit_count;
}

GList *
history_manager_get_most_visited(HistoryManager *self, int count)
{
    g_return_val_if_fail(HISTORY_IS_MANAGER(self), NULL);

    /* Sort by visit count */
    GList *sorted = g_list_copy(self->entries);
    sorted = g_list_sort(sorted, compare_visit_count);

    GList *result = NULL;
    int n = 0;
    for (GList *l = sorted; l && n < count; l = l->next, n++) {
        result = g_list_append(result, l->data);
    }
    g_list_free(sorted);
    return result;
}

GtkWidget *
history_manager_create_widget(HistoryManager *self)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 8);

    GtkWidget *title = gtk_label_new("History");
    gtk_widget_add_css_class(title, "title-3");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_append(GTK_BOX(box), title);

    /* Search entry */
    GtkWidget *search = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(search), "Search history...");
    gtk_box_append(GTK_BOX(box), search);

    /* History list */
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);

    GtkWidget *list_box = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box), GTK_SELECTION_NONE);

    for (GList *l = self->entries; l; l = l->next) {
        HistoryEntry *entry = l->data;

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_add_css_class(row, "sidebar-item");

        GtkWidget *icon = gtk_image_new_from_icon_name("document-open-recent-symbolic");
        gtk_box_append(GTK_BOX(row), icon);

        GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_hexpand(info_box, TRUE);

        GtkWidget *title_label = gtk_label_new(entry->title);
        gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
        gtk_label_set_xalign(GTK_LABEL(title_label), 0);
        gtk_box_append(GTK_BOX(info_box), title_label);

        GtkWidget *uri_label = gtk_label_new(entry->uri);
        gtk_label_set_ellipsize(GTK_LABEL(uri_label), PANGO_ELLIPSIZE_END);
        gtk_label_set_xalign(GTK_LABEL(uri_label), 0);
        gtk_widget_set_opacity(uri_label, 0.6);
        gtk_box_append(GTK_BOX(info_box), uri_label);

        gtk_box_append(GTK_BOX(row), info_box);
        gtk_list_box_append(GTK_LIST_BOX(list_box), row);
    }

    GtkWidget *placeholder = gtk_label_new("No history yet");
    gtk_widget_set_opacity(placeholder, 0.5);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(list_box), placeholder);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list_box);
    gtk_box_append(GTK_BOX(box), scrolled);

    /* Clear button */
    GtkWidget *clear_btn = gtk_button_new_with_label("Clear History");
    gtk_widget_add_css_class(clear_btn, "destructive-action");
    gtk_widget_set_margin_top(clear_btn, 8);
    gtk_box_append(GTK_BOX(box), clear_btn);

    return box;
}

static void
on_history_clear_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    HistoryManager *self = HISTORY_MANAGER(user_data);
    history_manager_clear(self);

    /* Remove the history file */
    if (self->data_file) {
        g_remove(self->data_file);
    }
}

GtkWidget *
history_manager_create_full_page(HistoryManager *self)
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

    GtkWidget *header_icon = gtk_image_new_from_icon_name("document-open-recent-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(header_icon), 32);
    gtk_box_append(GTK_BOX(header_box), header_icon);

    GtkWidget *header_text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title = gtk_label_new("History");
    gtk_widget_add_css_class(title, "title-1");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_append(GTK_BOX(header_text_box), title);

    GtkWidget *subtitle = gtk_label_new("Your browsing history");
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0);
    gtk_box_append(GTK_BOX(header_text_box), subtitle);

    gtk_widget_set_hexpand(header_text_box, TRUE);
    gtk_box_append(GTK_BOX(header_box), header_text_box);

    /* Clear history button */
    GtkWidget *clear_btn = gtk_button_new_with_label("Clear All");
    gtk_widget_add_css_class(clear_btn, "destructive-action");
    gtk_widget_set_valign(clear_btn, GTK_ALIGN_CENTER);
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_history_clear_clicked), self);
    gtk_box_append(GTK_BOX(header_box), clear_btn);

    gtk_box_append(GTK_BOX(page), header_box);

    /* Separator */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(page), sep);

    /* History list */
    GList *all_entries = history_manager_get_all(self);

    if (!all_entries) {
        /* Empty state */
        GtkWidget *empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margin_top(empty_box, 60);
        gtk_widget_set_margin_bottom(empty_box, 60);
        gtk_widget_set_halign(empty_box, GTK_ALIGN_CENTER);

        GtkWidget *empty_icon = gtk_image_new_from_icon_name("document-open-recent-symbolic");
        gtk_image_set_pixel_size(GTK_IMAGE(empty_icon), 64);
        gtk_widget_set_opacity(empty_icon, 0.3);
        gtk_box_append(GTK_BOX(empty_box), empty_icon);

        GtkWidget *empty_title = gtk_label_new("No history yet");
        gtk_widget_add_css_class(empty_title, "title-3");
        gtk_widget_set_opacity(empty_title, 0.5);
        gtk_box_append(GTK_BOX(empty_box), empty_title);

        GtkWidget *empty_desc = gtk_label_new("Pages you visit will appear here");
        gtk_widget_set_opacity(empty_desc, 0.4);
        gtk_box_append(GTK_BOX(empty_box), empty_desc);

        gtk_box_append(GTK_BOX(page), empty_box);
    } else {
        GtkWidget *list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

        int count = 0;
        for (GList *l = all_entries; l && count < 30; l = l->next, count++) {
            HistoryEntry *entry = l->data;
            if (!entry->title || !entry->uri) continue;

            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_widget_add_css_class(row, "history-row");

            GtkWidget *icon = gtk_image_new_from_icon_name("document-open-recent-symbolic");
            gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
            gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
            gtk_box_append(GTK_BOX(row), icon);

            GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
            gtk_widget_set_hexpand(text_box, TRUE);

            GtkWidget *title_label = gtk_label_new(entry->title);
            gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
            gtk_label_set_xalign(GTK_LABEL(title_label), 0);
            gtk_widget_add_css_class(title_label, "history-title");
            gtk_box_append(GTK_BOX(text_box), title_label);

            GtkWidget *uri_label = gtk_label_new(entry->uri);
            gtk_label_set_ellipsize(GTK_LABEL(uri_label), PANGO_ELLIPSIZE_END);
            gtk_label_set_xalign(GTK_LABEL(uri_label), 0);
            gtk_widget_set_opacity(uri_label, 0.5);
            gtk_widget_add_css_class(uri_label, "caption");
            gtk_box_append(GTK_BOX(text_box), uri_label);

            gtk_box_append(GTK_BOX(row), text_box);

            if (entry->visit_count > 1) {
                char *visits = g_strdup_printf("%d", entry->visit_count);
                GtkWidget *badge = gtk_label_new(visits);
                gtk_widget_add_css_class(badge, "dim-label");
                gtk_widget_set_valign(badge, GTK_ALIGN_CENTER);
                gtk_box_append(GTK_BOX(row), badge);
                g_free(visits);
            }

            gtk_box_append(GTK_BOX(list_box), row);
        }

        gtk_box_append(GTK_BOX(page), list_box);

        int total = g_list_length(all_entries);
        char *count_text = g_strdup_printf("Showing %d of %d entries", count, total);
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
