#ifndef PASSWORD_MANAGER_H
#define PASSWORD_MANAGER_H

#include <adwaita.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

#define PASSWORD_TYPE_MANAGER (password_manager_get_type())
G_DECLARE_FINAL_TYPE(PasswordManager, password_manager, PASSWORD, MANAGER, GObject)

typedef struct _PasswordEntry {
    char *site;
    char *username;
    char *password;
    gint64 saved_time;
} PasswordEntry;

PasswordManager *password_manager_get_default(void);
void password_manager_add(PasswordManager *self, const char *site, const char *username, const char *password);
void password_manager_remove(PasswordManager *self, const char *site, const char *username);
PasswordEntry *password_manager_find(PasswordManager *self, const char *site);
GList *password_manager_get_all(PasswordManager *self);
GtkWidget *password_manager_create_full_page(PasswordManager *self);

G_END_DECLS

#endif /* PASSWORD_MANAGER_H */
