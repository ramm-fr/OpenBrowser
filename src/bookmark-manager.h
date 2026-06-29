#ifndef BOOKMARK_MANAGER_H
#define BOOKMARK_MANAGER_H

#include <adwaita.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

#define BOOKMARK_TYPE_MANAGER (bookmark_manager_get_type())
G_DECLARE_FINAL_TYPE(BookmarkManager, bookmark_manager, BOOKMARK, MANAGER, GObject)

typedef struct _BookmarkItem {
    char *title;
    char *uri;
    char *folder;
    gint64 timestamp;
} BookmarkItem;

BookmarkManager *bookmark_manager_get_default(void);
void bookmark_manager_add(BookmarkManager *self, const char *title, const char *uri, const char *folder);
void bookmark_manager_remove(BookmarkManager *self, const char *uri);
gboolean bookmark_manager_exists(BookmarkManager *self, const char *uri);
GList *bookmark_manager_get_all(BookmarkManager *self);
GList *bookmark_manager_get_folder(BookmarkManager *self, const char *folder);
GList *bookmark_manager_search(BookmarkManager *self, const char *query);
void bookmark_manager_add_folder(BookmarkManager *self, const char *name);
void bookmark_manager_remove_folder(BookmarkManager *self, const char *name);
GList *bookmark_manager_get_folders(BookmarkManager *self);
void bookmark_manager_export_html(BookmarkManager *self, const char *path);
void bookmark_manager_import_html(BookmarkManager *self, const char *path);
GtkWidget *bookmark_manager_create_widget(BookmarkManager *self);
GtkWidget *bookmark_manager_create_bar(BookmarkManager *self);
GtkWidget *bookmark_manager_create_full_page(BookmarkManager *self);

G_END_DECLS

#endif /* BOOKMARK_MANAGER_H */
