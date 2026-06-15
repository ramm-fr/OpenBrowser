#include "password-manager.h"
#include "browser-window.h"
#include <string.h>

struct _PasswordManager {
    GObject parent_instance;
    GList *entries;
    char *data_file;
};

G_DEFINE_TYPE(PasswordManager, password_manager, G_TYPE_OBJECT)

static PasswordManager *default_instance = NULL;

/* Simple XOR encode/decode for basic obfuscation */
static char *
encode_password(const char *plain)
{
    const char key = 0x5A;
    gsize len = strlen(plain);
    char *encoded = g_malloc(len * 2 + 1);
    for (gsize i = 0; i < len; i++) {
        sprintf(encoded + i * 2, "%02x", (unsigned char)(plain[i] ^ key));
    }
    encoded[len * 2] = '\0';
    return encoded;
}

static char *
decode_password(const char *encoded)
{
    const char key = 0x5A;
    gsize len = strlen(encoded) / 2;
    char *plain = g_malloc(len + 1);
    for (gsize i = 0; i < len; i++) {
        unsigned int byte;
        sscanf(encoded + i * 2, "%02x", &byte);
        plain[i] = (char)(byte ^ key);
    }
    plain[len] = '\0';
    return plain;
}

static void
password_entry_free(PasswordEntry *entry)
{
    g_free(entry->site);
    g_free(entry->username);
    g_free(entry->password);
    g_free(entry);
}

static void
password_manager_load(PasswordManager *self)
{
    if (!g_file_test(self->data_file, G_FILE_TEST_EXISTS))
        return;

    JsonParser *parser = json_parser_new();
    GError *error = NULL;

    if (!json_parser_load_from_file(parser, self->data_file, &error)) {
        g_warning("Failed to load passwords: %s", error->message);
        g_error_free(error);
        g_object_unref(parser);
        return;
    }

    JsonNode *root = json_parser_get_root(parser);
    JsonArray *array = json_node_get_array(root);

    for (guint i = 0; i < json_array_get_length(array); i++) {
        JsonObject *obj = json_array_get_object_element(array, i);
        PasswordEntry *entry = g_new0(PasswordEntry, 1);
        entry->site = g_strdup(json_object_get_string_member(obj, "site"));
        entry->username = g_strdup(json_object_get_string_member(obj, "username"));
        const char *enc = json_object_get_string_member(obj, "password");
        entry->password = decode_password(enc);
        entry->saved_time = json_object_get_int_member(obj, "saved_time");
        self->entries = g_list_append(self->entries, entry);
    }

    g_object_unref(parser);
}

static void
password_manager_save(PasswordManager *self)
{
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_array(builder);

    for (GList *l = self->entries; l; l = l->next) {
        PasswordEntry *entry = l->data;
        char *enc = encode_password(entry->password);

        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "site");
        json_builder_add_string_value(builder, entry->site);
        json_builder_set_member_name(builder, "username");
        json_builder_add_string_value(builder, entry->username);
        json_builder_set_member_name(builder, "password");
        json_builder_add_string_value(builder, enc);
        json_builder_set_member_name(builder, "saved_time");
        json_builder_add_int_value(builder, entry->saved_time);
        json_builder_end_object(builder);

        g_free(enc);
    }

    json_builder_end_array(builder);

    JsonGenerator *gen = json_generator_new();
    json_generator_set_pretty(gen, TRUE);
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(gen, root);

    char *dir = g_path_get_dirname(self->data_file);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);

    GError *error = NULL;
    if (!json_generator_to_file(gen, self->data_file, &error)) {
        g_warning("Failed to save passwords: %s", error->message);
        g_error_free(error);
    }

    /* Set file permissions to owner-only */
    g_chmod(self->data_file, 0600);

    json_node_unref(root);
    g_object_unref(gen);
    g_object_unref(builder);
}

static void
password_manager_dispose(GObject *object)
{
    PasswordManager *self = PASSWORD_MANAGER(object);
    password_manager_save(self);
    g_list_free_full(self->entries, (GDestroyNotify)password_entry_free);
    self->entries = NULL;
    g_free(self->data_file);
    G_OBJECT_CLASS(password_manager_parent_class)->dispose(object);
}

static void
password_manager_class_init(PasswordManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = password_manager_dispose;
}

static void
password_manager_init(PasswordManager *self)
{
    self->entries = NULL;
    self->data_file = g_build_filename(g_get_user_data_dir(), "openbrowser", "passwords.json", NULL);
    password_manager_load(self);
}

PasswordManager *
password_manager_get_default(void)
{
    if (!default_instance) {
        default_instance = g_object_new(PASSWORD_TYPE_MANAGER, NULL);
    }
    return default_instance;
}

void
password_manager_add(PasswordManager *self, const char *site, const char *username, const char *password)
{
    g_return_if_fail(PASSWORD_IS_MANAGER(self));

    /* Update existing or add new */
    for (GList *l = self->entries; l; l = l->next) {
        PasswordEntry *entry = l->data;
        if (g_strcmp0(entry->site, site) == 0 && g_strcmp0(entry->username, username) == 0) {
            g_free(entry->password);
            entry->password = g_strdup(password);
            entry->saved_time = g_get_real_time() / G_USEC_PER_SEC;
            password_manager_save(self);
            return;
        }
    }

    PasswordEntry *entry = g_new0(PasswordEntry, 1);
    entry->site = g_strdup(site);
    entry->username = g_strdup(username);
    entry->password = g_strdup(password);
    entry->saved_time = g_get_real_time() / G_USEC_PER_SEC;
    self->entries = g_list_append(self->entries, entry);
    password_manager_save(self);
}

void
password_manager_remove(PasswordManager *self, const char *site, const char *username)
{
    g_return_if_fail(PASSWORD_IS_MANAGER(self));

    for (GList *l = self->entries; l; l = l->next) {
        PasswordEntry *entry = l->data;
        if (g_strcmp0(entry->site, site) == 0 &&
            (username == NULL || g_strcmp0(entry->username, username) == 0)) {
            self->entries = g_list_delete_link(self->entries, l);
            password_entry_free(entry);
            password_manager_save(self);
            return;
        }
    }
}

PasswordEntry *
password_manager_find(PasswordManager *self, const char *site)
{
    g_return_val_if_fail(PASSWORD_IS_MANAGER(self), NULL);

    for (GList *l = self->entries; l; l = l->next) {
        PasswordEntry *entry = l->data;
        if (g_strstr_len(site, -1, entry->site) || g_strstr_len(entry->site, -1, site)) {
            return entry;
        }
    }
    return NULL;
}

GList *
password_manager_get_all(PasswordManager *self)
{
    g_return_val_if_fail(PASSWORD_IS_MANAGER(self), NULL);
    return self->entries;
}

/* UI callbacks */
static void
on_password_delete_clicked(GtkButton *button, gpointer user_data)
{
    PasswordManager *self = PASSWORD_MANAGER(user_data);
    const char *site = g_object_get_data(G_OBJECT(button), "site");
    const char *username = g_object_get_data(G_OBJECT(button), "username");
    GtkWidget *row = g_object_get_data(G_OBJECT(button), "row-widget");

    if (site) {
        password_manager_remove(self, site, username);
        if (row) gtk_widget_set_visible(row, FALSE);
    }
}

static void
on_password_show_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    GtkWidget *label = g_object_get_data(G_OBJECT(button), "pass-label");
    const char *password = g_object_get_data(G_OBJECT(button), "password");

    if (label && password) {
        const char *current = gtk_label_get_text(GTK_LABEL(label));
        if (g_str_has_prefix(current, "•")) {
            gtk_label_set_text(GTK_LABEL(label), password);
            gtk_button_set_icon_name(GTK_BUTTON(button), "view-reveal-symbolic");
        } else {
            gtk_label_set_text(GTK_LABEL(label), "••••••••");
            gtk_button_set_icon_name(GTK_BUTTON(button), "view-conceal-symbolic");
        }
    }
}

static void
on_add_password_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    PasswordManager *self = PASSWORD_MANAGER(user_data);
    GtkWidget *widget = GTK_WIDGET(button);
    GtkRoot *root = gtk_widget_get_root(widget);

    /* Get entries from the add form */
    GtkWidget *site_entry = g_object_get_data(G_OBJECT(button), "site-entry");
    GtkWidget *user_entry = g_object_get_data(G_OBJECT(button), "user-entry");
    GtkWidget *pass_entry = g_object_get_data(G_OBJECT(button), "pass-entry");

    if (!site_entry || !user_entry || !pass_entry) return;

    const char *site = gtk_editable_get_text(GTK_EDITABLE(site_entry));
    const char *username = gtk_editable_get_text(GTK_EDITABLE(user_entry));
    const char *password = gtk_editable_get_text(GTK_EDITABLE(pass_entry));

    if (site && *site && username && *username && password && *password) {
        password_manager_add(self, site, username, password);
        gtk_editable_set_text(GTK_EDITABLE(site_entry), "");
        gtk_editable_set_text(GTK_EDITABLE(user_entry), "");
        gtk_editable_set_text(GTK_EDITABLE(pass_entry), "");

        /* Reopen passwords page to refresh */
        if (root && BROWSER_IS_WINDOW(root)) {
            /* Just show a confirmation for now */
        }
    }
}

GtkWidget *
password_manager_create_full_page(PasswordManager *self)
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
    GtkWidget *header_icon = gtk_image_new_from_icon_name("dialog-password-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(header_icon), 32);
    gtk_box_append(GTK_BOX(header_box), header_icon);

    GtkWidget *header_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title = gtk_label_new("Passwords");
    gtk_widget_add_css_class(title, "title-1");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_append(GTK_BOX(header_text), title);

    GtkWidget *subtitle = gtk_label_new("Your saved login credentials");
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0);
    gtk_box_append(GTK_BOX(header_text), subtitle);

    gtk_widget_set_hexpand(header_text, TRUE);
    gtk_box_append(GTK_BOX(header_box), header_text);
    gtk_box_append(GTK_BOX(page), header_box);

    /* Separator */
    gtk_box_append(GTK_BOX(page), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* Add new password form */
    GtkWidget *form_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(form_box, "history-row");

    GtkWidget *form_title = gtk_label_new("Add Password");
    gtk_label_set_xalign(GTK_LABEL(form_title), 0);
    gtk_widget_add_css_class(form_title, "heading");
    gtk_box_append(GTK_BOX(form_box), form_title);

    GtkWidget *form_grid = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *site_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(site_entry), "Website");
    gtk_widget_set_hexpand(site_entry, TRUE);
    gtk_box_append(GTK_BOX(form_grid), site_entry);

    GtkWidget *user_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(user_entry), "Username");
    gtk_widget_set_hexpand(user_entry, TRUE);
    gtk_box_append(GTK_BOX(form_grid), user_entry);

    GtkWidget *pass_entry = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(pass_entry), TRUE);
    gtk_widget_set_hexpand(pass_entry, TRUE);
    gtk_box_append(GTK_BOX(form_grid), pass_entry);

    GtkWidget *add_btn = gtk_button_new_with_label("Save");
    gtk_widget_add_css_class(add_btn, "suggested-action");
    g_object_set_data(G_OBJECT(add_btn), "site-entry", site_entry);
    g_object_set_data(G_OBJECT(add_btn), "user-entry", user_entry);
    g_object_set_data(G_OBJECT(add_btn), "pass-entry", pass_entry);
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_password_clicked), self);
    gtk_box_append(GTK_BOX(form_grid), add_btn);

    gtk_box_append(GTK_BOX(form_box), form_grid);
    gtk_box_append(GTK_BOX(page), form_box);

    /* Password list */
    GList *all = password_manager_get_all(self);

    if (!all) {
        GtkWidget *empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margin_top(empty_box, 40);
        gtk_widget_set_halign(empty_box, GTK_ALIGN_CENTER);

        GtkWidget *empty_icon = gtk_image_new_from_icon_name("dialog-password-symbolic");
        gtk_image_set_pixel_size(GTK_IMAGE(empty_icon), 48);
        gtk_widget_set_opacity(empty_icon, 0.3);
        gtk_box_append(GTK_BOX(empty_box), empty_icon);

        GtkWidget *empty_label = gtk_label_new("No saved passwords");
        gtk_widget_set_opacity(empty_label, 0.5);
        gtk_box_append(GTK_BOX(empty_box), empty_label);

        gtk_box_append(GTK_BOX(page), empty_box);
    } else {
        GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_margin_top(list, 8);

        for (GList *l = all; l; l = l->next) {
            PasswordEntry *entry = l->data;

            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
            gtk_widget_add_css_class(row, "history-row");

            /* Site icon */
            GtkWidget *icon = gtk_image_new_from_icon_name("channel-secure-symbolic");
            gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
            gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
            gtk_box_append(GTK_BOX(row), icon);

            /* Info */
            GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            gtk_widget_set_hexpand(info, TRUE);

            GtkWidget *site_label = gtk_label_new(entry->site);
            gtk_label_set_xalign(GTK_LABEL(site_label), 0);
            gtk_widget_add_css_class(site_label, "history-title");
            gtk_box_append(GTK_BOX(info), site_label);

            GtkWidget *user_label = gtk_label_new(entry->username);
            gtk_label_set_xalign(GTK_LABEL(user_label), 0);
            gtk_widget_set_opacity(user_label, 0.6);
            gtk_widget_add_css_class(user_label, "caption");
            gtk_box_append(GTK_BOX(info), user_label);

            gtk_box_append(GTK_BOX(row), info);

            /* Password (hidden) */
            GtkWidget *pass_label = gtk_label_new("••••••••");
            gtk_widget_set_valign(pass_label, GTK_ALIGN_CENTER);
            gtk_widget_set_opacity(pass_label, 0.7);
            gtk_box_append(GTK_BOX(row), pass_label);

            /* Show/hide button */
            GtkWidget *show_btn = gtk_button_new_from_icon_name("view-conceal-symbolic");
            gtk_widget_add_css_class(show_btn, "flat");
            gtk_widget_add_css_class(show_btn, "circular");
            gtk_widget_set_valign(show_btn, GTK_ALIGN_CENTER);
            g_object_set_data(G_OBJECT(show_btn), "pass-label", pass_label);
            g_object_set_data_full(G_OBJECT(show_btn), "password", g_strdup(entry->password), g_free);
            g_signal_connect(show_btn, "clicked", G_CALLBACK(on_password_show_clicked), self);
            gtk_box_append(GTK_BOX(row), show_btn);

            /* Delete button */
            GtkWidget *del_btn = gtk_button_new_from_icon_name("user-trash-symbolic");
            gtk_widget_add_css_class(del_btn, "flat");
            gtk_widget_add_css_class(del_btn, "circular");
            gtk_widget_set_valign(del_btn, GTK_ALIGN_CENTER);
            g_object_set_data_full(G_OBJECT(del_btn), "site", g_strdup(entry->site), g_free);
            g_object_set_data_full(G_OBJECT(del_btn), "username", g_strdup(entry->username), g_free);
            g_object_set_data(G_OBJECT(del_btn), "row-widget", row);
            g_signal_connect(del_btn, "clicked", G_CALLBACK(on_password_delete_clicked), self);
            gtk_box_append(GTK_BOX(row), del_btn);

            gtk_box_append(GTK_BOX(list), row);
        }

        gtk_box_append(GTK_BOX(page), list);

        int count = g_list_length(all);
        char *count_text = g_strdup_printf("%d password%s saved", count, count == 1 ? "" : "s");
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
