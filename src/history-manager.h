#ifndef HISTORY_MANAGER_H
#define HISTORY_MANAGER_H

#include <adwaita.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

#define HISTORY_TYPE_MANAGER (history_manager_get_type())
G_DECLARE_FINAL_TYPE(HistoryManager, history_manager, HISTORY, MANAGER, GObject)

typedef struct _HistoryEntry {
    char *title;
    char *uri;
    gint64 visit_time;
    int visit_count;
} HistoryEntry;

HistoryManager *history_manager_get_default(void);
void history_manager_add(HistoryManager *self, const char *title, const char *uri);
void history_manager_remove(HistoryManager *self, const char *uri);
void history_manager_clear(HistoryManager *self);
void history_manager_clear_range(HistoryManager *self, gint64 start, gint64 end);
GList *history_manager_get_all(HistoryManager *self);
GList *history_manager_get_recent(HistoryManager *self, int count);
GList *history_manager_search(HistoryManager *self, const char *query);
GList *history_manager_get_most_visited(HistoryManager *self, int count);
GtkWidget *history_manager_create_widget(HistoryManager *self);
GtkWidget *history_manager_create_full_page(HistoryManager *self);

G_END_DECLS

#endif /* HISTORY_MANAGER_H */
