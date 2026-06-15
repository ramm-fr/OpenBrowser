#ifndef BROWSER_TAB_H
#define BROWSER_TAB_H

#include <adwaita.h>
#include <webkit/webkit.h>

G_BEGIN_DECLS

#define BROWSER_TYPE_TAB (browser_tab_get_type())
G_DECLARE_FINAL_TYPE(BrowserTab, browser_tab, BROWSER, TAB, GtkBox)

BrowserTab *browser_tab_new(const char *uri, gboolean is_private);
WebKitWebView *browser_tab_get_web_view(BrowserTab *self);
const char *browser_tab_get_uri(BrowserTab *self);
const char *browser_tab_get_title(BrowserTab *self);
gboolean browser_tab_is_loading(BrowserTab *self);
gboolean browser_tab_is_private(BrowserTab *self);
gboolean browser_tab_is_pinned(BrowserTab *self);
gboolean browser_tab_is_muted(BrowserTab *self);
void browser_tab_set_pinned(BrowserTab *self, gboolean pinned);
void browser_tab_set_muted(BrowserTab *self, gboolean muted);
double browser_tab_get_zoom_level(BrowserTab *self);
void browser_tab_set_zoom_level(BrowserTab *self, double level);
void browser_tab_navigate(BrowserTab *self, const char *uri);
void browser_tab_go_back(BrowserTab *self);
void browser_tab_go_forward(BrowserTab *self);
void browser_tab_reload(BrowserTab *self);
void browser_tab_hard_reload(BrowserTab *self);
void browser_tab_stop_loading(BrowserTab *self);
void browser_tab_find_text(BrowserTab *self, const char *text);
void browser_tab_find_next(BrowserTab *self);
void browser_tab_find_previous(BrowserTab *self);
void browser_tab_find_close(BrowserTab *self);

G_END_DECLS

#endif /* BROWSER_TAB_H */
