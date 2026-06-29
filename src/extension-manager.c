#include "extension-manager.h"
#include "browser-window.h"
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <glib/gstdio.h>
#include <string.h>

/* Default store catalog URL (host this JSON in the OpenBrowser repo) */
#define STORE_CATALOG_URL "https://raw.githubusercontent.com/ramm-fr/OpenBrowser/main/store/extensions.json"

struct _ExtensionManager {
    GObject parent_instance;
    GList *installed;     /* Extension* loaded from disk */
    char *ext_dir;        /* ~/.local/share/openbrowser/extensions */
    SoupSession *session;
};

G_DEFINE_TYPE(ExtensionManager, extension_manager, G_TYPE_OBJECT)

static ExtensionManager *default_instance = NULL;

void
extension_free(Extension *e)
{
    if (!e) return;
    g_free(e->id);
    g_free(e->name);
    g_free(e->description);
    g_free(e->author);
    g_free(e->version);
    g_free(e->icon);
    g_free(e->script);
    g_free(e->style);
    g_free(e);
}

void
extension_free_list(GList *list)
{
    g_list_free_full(list, (GDestroyNotify)extension_free);
}

/* Parse a JSON object node into an Extension */
static Extension *
extension_from_json(JsonObject *obj)
{
    Extension *e = g_new0(Extension, 1);
    e->id = g_strdup(json_object_get_string_member_with_default(obj, "id", ""));
    e->name = g_strdup(json_object_get_string_member_with_default(obj, "name", "Unnamed"));
    e->description = g_strdup(json_object_get_string_member_with_default(obj, "description", ""));
    e->author = g_strdup(json_object_get_string_member_with_default(obj, "author", "Unknown"));
    e->version = g_strdup(json_object_get_string_member_with_default(obj, "version", "1.0.0"));
    e->icon = g_strdup(json_object_get_string_member_with_default(obj, "icon", "application-x-addon-symbolic"));
    if (json_object_has_member(obj, "script") &&
        !json_object_get_null_member(obj, "script"))
        e->script = g_strdup(json_object_get_string_member(obj, "script"));
    if (json_object_has_member(obj, "style") &&
        !json_object_get_null_member(obj, "style"))
        e->style = g_strdup(json_object_get_string_member(obj, "style"));
    e->enabled = json_object_get_boolean_member_with_default(obj, "enabled", TRUE);
    e->pinned = json_object_get_boolean_member_with_default(obj, "pinned", FALSE);
    return e;
}

/* Serialize an Extension to a JSON object (for disk storage) */
static void
extension_to_builder(Extension *e, JsonBuilder *b)
{
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "id"); json_builder_add_string_value(b, e->id);
    json_builder_set_member_name(b, "name"); json_builder_add_string_value(b, e->name);
    json_builder_set_member_name(b, "description"); json_builder_add_string_value(b, e->description);
    json_builder_set_member_name(b, "author"); json_builder_add_string_value(b, e->author);
    json_builder_set_member_name(b, "version"); json_builder_add_string_value(b, e->version);
    json_builder_set_member_name(b, "icon"); json_builder_add_string_value(b, e->icon);
    json_builder_set_member_name(b, "script");
    if (e->script) json_builder_add_string_value(b, e->script); else json_builder_add_null_value(b);
    json_builder_set_member_name(b, "style");
    if (e->style) json_builder_add_string_value(b, e->style); else json_builder_add_null_value(b);
    json_builder_set_member_name(b, "enabled"); json_builder_add_boolean_value(b, e->enabled);
    json_builder_set_member_name(b, "pinned"); json_builder_add_boolean_value(b, e->pinned);
    json_builder_end_object(b);
}

static Extension *
find_installed(ExtensionManager *self, const char *id)
{
    for (GList *l = self->installed; l; l = l->next) {
        Extension *e = l->data;
        if (g_strcmp0(e->id, id) == 0) return e;
    }
    return NULL;
}

/* ===== Disk persistence ===== */

static char *
ext_path(ExtensionManager *self, const char *id)
{
    char *fname = g_strdup_printf("%s.json", id);
    char *path = g_build_filename(self->ext_dir, fname, NULL);
    g_free(fname);
    return path;
}

static void
save_extension(ExtensionManager *self, Extension *e)
{
    g_mkdir_with_parents(self->ext_dir, 0755);
    JsonBuilder *b = json_builder_new();
    extension_to_builder(e, b);
    JsonGenerator *gen = json_generator_new();
    json_generator_set_pretty(gen, TRUE);
    JsonNode *root = json_builder_get_root(b);
    json_generator_set_root(gen, root);
    char *path = ext_path(self, e->id);
    json_generator_to_file(gen, path, NULL);
    g_free(path);
    json_node_unref(root);
    g_object_unref(gen);
    g_object_unref(b);
}

static void
load_installed(ExtensionManager *self)
{
    GDir *dir = g_dir_open(self->ext_dir, 0, NULL);
    if (!dir) return;
    const char *name;
    while ((name = g_dir_read_name(dir))) {
        if (!g_str_has_suffix(name, ".json")) continue;
        char *path = g_build_filename(self->ext_dir, name, NULL);
        JsonParser *parser = json_parser_new();
        if (json_parser_load_from_file(parser, path, NULL)) {
            JsonNode *root = json_parser_get_root(parser);
            if (root && JSON_NODE_HOLDS_OBJECT(root)) {
                Extension *e = extension_from_json(json_node_get_object(root));
                e->installed = TRUE;
                self->installed = g_list_append(self->installed, e);
            }
        }
        g_object_unref(parser);
        g_free(path);
    }
    g_dir_close(dir);
}

/* ===== Lifecycle ===== */

static void
extension_manager_dispose(GObject *object)
{
    ExtensionManager *self = EXTENSION_MANAGER(object);
    g_list_free_full(self->installed, (GDestroyNotify)extension_free);
    self->installed = NULL;
    g_clear_object(&self->session);
    g_free(self->ext_dir);
    G_OBJECT_CLASS(extension_manager_parent_class)->dispose(object);
}

static void
extension_manager_class_init(ExtensionManagerClass *klass)
{
    G_OBJECT_CLASS(klass)->dispose = extension_manager_dispose;
}

static void
extension_manager_init(ExtensionManager *self)
{
    self->installed = NULL;
    self->session = soup_session_new();
    self->ext_dir = g_build_filename(g_get_user_data_dir(), "openbrowser", "extensions", NULL);
    load_installed(self);
}

ExtensionManager *
extension_manager_get_default(void)
{
    if (!default_instance)
        default_instance = g_object_new(EXTENSION_TYPE_MANAGER, NULL);
    return default_instance;
}

/* ===== Public API ===== */

GList *
extension_manager_get_installed(ExtensionManager *self)
{
    g_return_val_if_fail(EXTENSION_IS_MANAGER(self), NULL);
    return self->installed;
}

gboolean
extension_manager_is_installed(ExtensionManager *self, const char *id)
{
    return find_installed(self, id) != NULL;
}

void
extension_manager_install(ExtensionManager *self, Extension *ext)
{
    g_return_if_fail(EXTENSION_IS_MANAGER(self));
    if (find_installed(self, ext->id)) return;

    Extension *e = g_new0(Extension, 1);
    e->id = g_strdup(ext->id);
    e->name = g_strdup(ext->name);
    e->description = g_strdup(ext->description);
    e->author = g_strdup(ext->author);
    e->version = g_strdup(ext->version);
    e->icon = g_strdup(ext->icon);
    e->script = ext->script ? g_strdup(ext->script) : NULL;
    e->style = ext->style ? g_strdup(ext->style) : NULL;
    e->enabled = TRUE;
    e->installed = TRUE;
    self->installed = g_list_append(self->installed, e);
    save_extension(self, e);
}

void
extension_manager_uninstall(ExtensionManager *self, const char *id)
{
    g_return_if_fail(EXTENSION_IS_MANAGER(self));
    Extension *e = find_installed(self, id);
    if (!e) return;
    char *path = ext_path(self, id);
    g_remove(path);
    g_free(path);
    self->installed = g_list_remove(self->installed, e);
    extension_free(e);
}

/* Update an installed extension to the catalog version, keeping enabled state */
void
extension_manager_update(ExtensionManager *self, Extension *catalog_ext)
{
    g_return_if_fail(EXTENSION_IS_MANAGER(self));
    Extension *e = find_installed(self, catalog_ext->id);
    if (!e) return;

    gboolean was_enabled = e->enabled;
    g_free(e->name); e->name = g_strdup(catalog_ext->name);
    g_free(e->description); e->description = g_strdup(catalog_ext->description);
    g_free(e->author); e->author = g_strdup(catalog_ext->author);
    g_free(e->version); e->version = g_strdup(catalog_ext->version);
    g_free(e->icon); e->icon = g_strdup(catalog_ext->icon);
    g_free(e->script); e->script = catalog_ext->script ? g_strdup(catalog_ext->script) : NULL;
    g_free(e->style); e->style = catalog_ext->style ? g_strdup(catalog_ext->style) : NULL;
    e->enabled = was_enabled;
    save_extension(self, e);
}

void
extension_manager_set_enabled(ExtensionManager *self, const char *id, gboolean enabled)
{
    g_return_if_fail(EXTENSION_IS_MANAGER(self));
    Extension *e = find_installed(self, id);
    if (e) { e->enabled = enabled; save_extension(self, e); }
}

gboolean
extension_manager_is_enabled(ExtensionManager *self, const char *id)
{
    Extension *e = find_installed(self, id);
    return e ? e->enabled : FALSE;
}

void
extension_manager_set_pinned(ExtensionManager *self, const char *id, gboolean pinned)
{
    g_return_if_fail(EXTENSION_IS_MANAGER(self));
    Extension *e = find_installed(self, id);
    if (e) { e->pinned = pinned; save_extension(self, e); }
}

gboolean
extension_manager_is_pinned(ExtensionManager *self, const char *id)
{
    Extension *e = find_installed(self, id);
    return e ? e->pinned : FALSE;
}

void
extension_manager_apply(ExtensionManager *self, WebKitUserContentManager *ucm)
{
    g_return_if_fail(EXTENSION_IS_MANAGER(self));
    if (!ucm) return;
    for (GList *l = self->installed; l; l = l->next) {
        Extension *e = l->data;
        if (!e->enabled) continue;
        if (e->style) {
            WebKitUserStyleSheet *ss = webkit_user_style_sheet_new(
                e->style, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                WEBKIT_USER_STYLE_LEVEL_USER, NULL, NULL);
            webkit_user_content_manager_add_style_sheet(ucm, ss);
            webkit_user_style_sheet_unref(ss);
        }
        if (e->script) {
            WebKitUserScript *us = webkit_user_script_new(
                e->script, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, NULL, NULL);
            webkit_user_content_manager_add_script(ucm, us);
            webkit_user_script_unref(us);
        }
    }
}

/* ===== Catalog fetching ===== */

typedef struct {
    ExtensionManager *self;
    ExtensionCatalogCb cb;
    gpointer user_data;
} CatalogCtx;

static void
on_catalog_loaded(GObject *source, GAsyncResult *result, gpointer user_data)
{
    CatalogCtx *ctx = user_data;
    GError *error = NULL;
    GBytes *body = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);

    GList *catalog = NULL;
    if (body) {
        gsize size = 0;
        const char *data = g_bytes_get_data(body, &size);
        JsonParser *parser = json_parser_new();
        if (json_parser_load_from_data(parser, data, size, NULL)) {
            JsonNode *root = json_parser_get_root(parser);
            JsonArray *arr = NULL;
            if (root && JSON_NODE_HOLDS_OBJECT(root)) {
                JsonObject *o = json_node_get_object(root);
                if (json_object_has_member(o, "extensions"))
                    arr = json_object_get_array_member(o, "extensions");
            } else if (root && JSON_NODE_HOLDS_ARRAY(root)) {
                arr = json_node_get_array(root);
            }
            if (arr) {
                for (guint i = 0; i < json_array_get_length(arr); i++) {
                    JsonObject *eo = json_array_get_object_element(arr, i);
                    catalog = g_list_append(catalog, extension_from_json(eo));
                }
            }
        }
        g_object_unref(parser);
        g_bytes_unref(body);
    }
    if (error) g_error_free(error);

    ctx->cb(catalog, ctx->user_data);
    g_free(ctx);
}

void
extension_manager_fetch_catalog(ExtensionManager *self, ExtensionCatalogCb cb, gpointer user_data)
{
    g_return_if_fail(EXTENSION_IS_MANAGER(self));
    CatalogCtx *ctx = g_new0(CatalogCtx, 1);
    ctx->self = self; ctx->cb = cb; ctx->user_data = user_data;

    SoupMessage *msg = soup_message_new("GET", STORE_CATALOG_URL);
    soup_session_send_and_read_async(self->session, msg, G_PRIORITY_DEFAULT, NULL,
        on_catalog_loaded, ctx);
    g_object_unref(msg);
}

/* ===== Store page UI ===== */

typedef struct {
    ExtensionManager *self;
    GtkWidget *store_list;     /* container for catalog cards */
    GtkWidget *installed_list; /* container for installed cards */
    GtkWidget *store_spinner;
} PageCtx;

static GtkWidget *build_installed_card(PageCtx *pc, Extension *e);
static void refresh_installed(PageCtx *pc);

static void
toast(GtkWidget *w, const char *msg)
{
    GtkRoot *root = gtk_widget_get_root(w);
    GtkWidget *overlay = root ? g_object_get_data(G_OBJECT(root), "toast-overlay") : NULL;
    if (overlay && ADW_IS_TOAST_OVERLAY(overlay)) {
        AdwToast *t = adw_toast_new(msg);
        adw_toast_set_timeout(t, 2);
        adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(overlay), t);
    }
}

/* Re-apply extensions to all open tabs in real time */
static void
reapply_all(GtkWidget *w)
{
    GtkRoot *root = gtk_widget_get_root(w);
    if (root && BROWSER_IS_WINDOW(root))
        browser_window_reapply_extensions(BROWSER_WINDOW(root));
}

static void
on_installed_toggle(GObject *sw, GParamSpec *pspec, gpointer user_data)
{
    (void)pspec;
    PageCtx *pc = user_data;
    const char *id = g_object_get_data(sw, "ext-id");
    gboolean active = gtk_switch_get_active(GTK_SWITCH(sw));
    extension_manager_set_enabled(pc->self, id, active);
    reapply_all(GTK_WIDGET(sw));
    toast(GTK_WIDGET(sw), active ? "Enabled — applied to all tabs"
                                 : "Disabled — applied to all tabs");
}

static void
on_uninstall_clicked(GtkButton *btn, gpointer user_data)
{
    PageCtx *pc = user_data;
    const char *id = g_object_get_data(G_OBJECT(btn), "ext-id");
    extension_manager_uninstall(pc->self, id);
    refresh_installed(pc);
    reapply_all(GTK_WIDGET(btn));
    toast(GTK_WIDGET(btn), "Extension removed — applied to all tabs");
}

/* Catalog "Install" button payload */
typedef struct { PageCtx *pc; Extension *ext; GtkWidget *btn; } InstallCtx;

static void
on_install_clicked(GtkButton *btn, gpointer user_data)
{
    InstallCtx *ic = user_data;
    extension_manager_install(ic->pc->self, ic->ext);
    gtk_button_set_label(btn, "Installed");
    gtk_widget_remove_css_class(GTK_WIDGET(btn), "suggested-action");
    gtk_widget_set_sensitive(GTK_WIDGET(btn), FALSE);
    refresh_installed(ic->pc);
    reapply_all(GTK_WIDGET(btn));
    toast(GTK_WIDGET(btn), "Installed — applied to all tabs");
}

static void
on_update_clicked(GtkButton *btn, gpointer user_data)
{
    InstallCtx *ic = user_data;
    extension_manager_update(ic->pc->self, ic->ext);
    gtk_button_set_label(btn, "Updated");
    gtk_widget_remove_css_class(GTK_WIDGET(btn), "suggested-action");
    gtk_widget_set_sensitive(GTK_WIDGET(btn), FALSE);
    refresh_installed(ic->pc);
    reapply_all(GTK_WIDGET(btn));
    toast(GTK_WIDGET(btn), "Updated — applied to all tabs");
}

static GtkWidget *
build_ext_row(Extension *e, GtkWidget *trailing)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_add_css_class(row, "ext-card");

    GtkWidget *icon = gtk_image_new_from_icon_name(e->icon ? e->icon : "application-x-addon-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 28);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(row), icon);

    GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_set_hexpand(info, TRUE);

    GtkWidget *titlerow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *name = gtk_label_new(e->name);
    gtk_label_set_xalign(GTK_LABEL(name), 0);
    gtk_widget_add_css_class(name, "ext-name");
    gtk_box_append(GTK_BOX(titlerow), name);

    char *meta = g_strdup_printf("v%s · %s", e->version ? e->version : "1.0", e->author ? e->author : "");
    GtkWidget *metalbl = gtk_label_new(meta);
    gtk_widget_add_css_class(metalbl, "dim-label");
    gtk_widget_add_css_class(metalbl, "caption");
    gtk_widget_set_valign(metalbl, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(titlerow), metalbl);
    g_free(meta);
    gtk_box_append(GTK_BOX(info), titlerow);

    GtkWidget *desc = gtk_label_new(e->description);
    gtk_label_set_xalign(GTK_LABEL(desc), 0);
    gtk_label_set_wrap(GTK_LABEL(desc), TRUE);
    gtk_widget_add_css_class(desc, "dim-label");
    gtk_widget_add_css_class(desc, "caption");
    gtk_box_append(GTK_BOX(info), desc);
    gtk_box_append(GTK_BOX(row), info);

    if (trailing) {
        gtk_widget_set_valign(trailing, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(row), trailing);
    }
    return row;
}

static GtkWidget *
build_installed_card(PageCtx *pc, Extension *e)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), e->enabled);
    g_object_set_data_full(G_OBJECT(sw), "ext-id", g_strdup(e->id), g_free);
    g_signal_connect(sw, "notify::active", G_CALLBACK(on_installed_toggle), pc);
    gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), sw);

    GtkWidget *rm = gtk_button_new_from_icon_name("user-trash-symbolic");
    gtk_widget_add_css_class(rm, "flat");
    gtk_widget_add_css_class(rm, "circular");
    gtk_widget_set_valign(rm, GTK_ALIGN_CENTER);
    g_object_set_data_full(G_OBJECT(rm), "ext-id", g_strdup(e->id), g_free);
    g_signal_connect(rm, "clicked", G_CALLBACK(on_uninstall_clicked), pc);
    gtk_box_append(GTK_BOX(box), rm);

    return build_ext_row(e, box);
}

static void
clear_box(GtkWidget *box)
{
    GtkWidget *c = gtk_widget_get_first_child(box);
    while (c) { GtkWidget *n = gtk_widget_get_next_sibling(c); gtk_box_remove(GTK_BOX(box), c); c = n; }
}

static void
refresh_installed(PageCtx *pc)
{
    clear_box(pc->installed_list);
    GList *inst = extension_manager_get_installed(pc->self);
    if (!inst) {
        GtkWidget *empty = gtk_label_new("No extensions installed yet. Browse the store above.");
        gtk_widget_add_css_class(empty, "dim-label");
        gtk_widget_set_margin_top(empty, 8);
        gtk_box_append(GTK_BOX(pc->installed_list), empty);
        return;
    }
    for (GList *l = inst; l; l = l->next)
        gtk_box_append(GTK_BOX(pc->installed_list), build_installed_card(pc, l->data));
}

/* Catalog fetch callback — populate the store list */
static void
on_catalog_ready(GList *catalog, gpointer user_data)
{
    PageCtx *pc = user_data;

    if (pc->store_spinner) {
        gtk_widget_set_visible(pc->store_spinner, FALSE);
    }
    clear_box(pc->store_list);

    if (!catalog) {
        GtkWidget *err = gtk_label_new("Couldn't load the store. Check your internet connection.");
        gtk_widget_add_css_class(err, "dim-label");
        gtk_box_append(GTK_BOX(pc->store_list), err);
        return;
    }

    for (GList *l = catalog; l; l = l->next) {
        Extension *e = l->data;
        gboolean installed = extension_manager_is_installed(pc->self, e->id);

        /* Find installed version to detect updates */
        const char *inst_ver = NULL;
        for (GList *il = extension_manager_get_installed(pc->self); il; il = il->next) {
            Extension *ie = il->data;
            if (g_strcmp0(ie->id, e->id) == 0) { inst_ver = ie->version; break; }
        }
        gboolean update_available = installed && inst_ver &&
            g_strcmp0(inst_ver, e->version) != 0;

        GtkWidget *btn;
        if (update_available) {
            btn = gtk_button_new_with_label("Update");
            gtk_widget_add_css_class(btn, "suggested-action");
        } else if (installed) {
            btn = gtk_button_new_with_label("Installed");
            gtk_widget_set_sensitive(btn, FALSE);
        } else {
            btn = gtk_button_new_with_label("Install");
            gtk_widget_add_css_class(btn, "suggested-action");
        }
        gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);

        /* Keep a copy of the extension alive for the action */
        Extension *copy = g_new0(Extension, 1);
        copy->id = g_strdup(e->id); copy->name = g_strdup(e->name);
        copy->description = g_strdup(e->description); copy->author = g_strdup(e->author);
        copy->version = g_strdup(e->version); copy->icon = g_strdup(e->icon);
        copy->script = e->script ? g_strdup(e->script) : NULL;
        copy->style = e->style ? g_strdup(e->style) : NULL;

        InstallCtx *ic = g_new0(InstallCtx, 1);
        ic->pc = pc; ic->ext = copy; ic->btn = btn;
        g_object_set_data_full(G_OBJECT(btn), "install-ctx", ic, g_free);
        g_object_set_data_full(G_OBJECT(btn), "ext-copy", copy, (GDestroyNotify)extension_free);
        if (update_available)
            g_signal_connect(btn, "clicked", G_CALLBACK(on_update_clicked), ic);
        else if (!installed)
            g_signal_connect(btn, "clicked", G_CALLBACK(on_install_clicked), ic);

        gtk_box_append(GTK_BOX(pc->store_list), build_ext_row(e, btn));
    }

    extension_free_list(catalog);
}

static void
page_ctx_free(gpointer data, GObject *where)
{
    (void)where;
    g_free(data);
}

GtkWidget *
extension_manager_create_page(ExtensionManager *self)
{
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);

    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(page, 40);
    gtk_widget_set_margin_end(page, 40);
    gtk_widget_set_margin_top(page, 32);
    gtk_widget_set_margin_bottom(page, 32);
    gtk_widget_set_halign(page, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(page, 660, -1);

    /* Header */
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    GtkWidget *header_icon = gtk_image_new_from_icon_name("application-x-addon-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(header_icon), 36);
    gtk_widget_set_valign(header_icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(header_box), header_icon);
    GtkWidget *header_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *title = gtk_label_new("Extensions");
    gtk_widget_add_css_class(title, "title-1");
    gtk_label_set_xalign(GTK_LABEL(title), 0);
    gtk_box_append(GTK_BOX(header_text), title);
    GtkWidget *subtitle = gtk_label_new("Install extensions from the OpenBrowser store");
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0);
    gtk_box_append(GTK_BOX(header_text), subtitle);
    gtk_widget_set_hexpand(header_text, TRUE);
    gtk_box_append(GTK_BOX(header_box), header_text);
    gtk_box_append(GTK_BOX(page), header_box);

    PageCtx *pc = g_new0(PageCtx, 1);
    pc->self = self;

    /* ===== Installed section ===== */
    GtkWidget *inst_title = gtk_label_new("Installed");
    gtk_widget_add_css_class(inst_title, "cert-section-title");
    gtk_widget_set_margin_top(inst_title, 22);
    gtk_widget_set_margin_bottom(inst_title, 8);
    gtk_label_set_xalign(GTK_LABEL(inst_title), 0);
    gtk_box_append(GTK_BOX(page), inst_title);

    pc->installed_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(page), pc->installed_list);

    /* ===== Store section ===== */
    GtkWidget *store_head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(store_head, 28);
    gtk_widget_set_margin_bottom(store_head, 8);
    GtkWidget *store_title = gtk_label_new("Store");
    gtk_widget_add_css_class(store_title, "cert-section-title");
    gtk_label_set_xalign(GTK_LABEL(store_title), 0);
    gtk_widget_set_hexpand(store_title, TRUE);
    gtk_box_append(GTK_BOX(store_head), store_title);
    pc->store_spinner = gtk_spinner_new();
    gtk_spinner_set_spinning(GTK_SPINNER(pc->store_spinner), TRUE);
    gtk_box_append(GTK_BOX(store_head), pc->store_spinner);
    gtk_box_append(GTK_BOX(page), store_head);

    pc->store_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(page), pc->store_list);

    /* Footer */
    GtkWidget *note = gtk_label_new(
        "Extensions are applied instantly to all open tabs. Updates appear here when available.");
    gtk_label_set_wrap(GTK_LABEL(note), TRUE);
    gtk_widget_add_css_class(note, "dim-label");
    gtk_widget_add_css_class(note, "caption");
    gtk_widget_set_margin_top(note, 24);
    gtk_widget_set_halign(note, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(page), note);

    /* Populate */
    refresh_installed(pc);
    extension_manager_fetch_catalog(self, on_catalog_ready, pc);

    /* Free pc when the page is destroyed */
    g_object_weak_ref(G_OBJECT(scrolled), page_ctx_free, pc);

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), page);
    return scrolled;
}
