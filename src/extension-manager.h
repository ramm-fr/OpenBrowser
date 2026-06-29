#ifndef EXTENSION_MANAGER_H
#define EXTENSION_MANAGER_H

#include <adwaita.h>
#include <webkit/webkit.h>

G_BEGIN_DECLS

#define EXTENSION_TYPE_MANAGER (extension_manager_get_type())
G_DECLARE_FINAL_TYPE(ExtensionManager, extension_manager, EXTENSION, MANAGER, GObject)

typedef struct _Extension {
    char *id;
    char *name;
    char *description;
    char *author;
    char *version;
    char *icon;        /* icon name */
    char *script;      /* JS payload (heap, or NULL) */
    char *style;       /* CSS payload (heap, or NULL) */
    gboolean enabled;
    gboolean installed;
    gboolean pinned;
} Extension;

ExtensionManager *extension_manager_get_default(void);

/* Installed extensions (owned by manager) */
GList *extension_manager_get_installed(ExtensionManager *self);

gboolean extension_manager_is_installed(ExtensionManager *self, const char *id);
void extension_manager_install(ExtensionManager *self, Extension *ext);
void extension_manager_update(ExtensionManager *self, Extension *catalog_ext);
void extension_manager_uninstall(ExtensionManager *self, const char *id);
void extension_manager_set_enabled(ExtensionManager *self, const char *id, gboolean enabled);
gboolean extension_manager_is_enabled(ExtensionManager *self, const char *id);
void extension_manager_set_pinned(ExtensionManager *self, const char *id, gboolean pinned);
gboolean extension_manager_is_pinned(ExtensionManager *self, const char *id);

/* Apply all enabled installed extensions to a web view's content manager */
void extension_manager_apply(ExtensionManager *self, WebKitUserContentManager *ucm);

/* Fetch the remote store catalog asynchronously.
 * callback receives a GList of Extension* (transfer full — caller frees with
 * extension_free_list) and the user_data. On error, list is NULL. */
typedef void (*ExtensionCatalogCb)(GList *catalog, gpointer user_data);
void extension_manager_fetch_catalog(ExtensionManager *self, ExtensionCatalogCb cb, gpointer user_data);

void extension_free(Extension *e);
void extension_free_list(GList *list);

/* Build the Extensions store page widget */
GtkWidget *extension_manager_create_page(ExtensionManager *self);

G_END_DECLS

#endif /* EXTENSION_MANAGER_H */
