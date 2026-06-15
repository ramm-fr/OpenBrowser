#ifndef BROWSER_WINDOW_H
#define BROWSER_WINDOW_H

#include <adwaita.h>
#include <webkit/webkit.h>

G_BEGIN_DECLS

#define BROWSER_TYPE_WINDOW (browser_window_get_type())
G_DECLARE_FINAL_TYPE(BrowserWindow, browser_window, BROWSER, WINDOW, AdwApplicationWindow)

BrowserWindow *browser_window_new(AdwApplication *app);
void browser_window_new_tab(BrowserWindow *self, const char *uri);
void browser_window_close_tab(BrowserWindow *self, int index);
void browser_window_set_tab(BrowserWindow *self, int index);
int browser_window_get_current_tab(BrowserWindow *self);
int browser_window_get_tab_count(BrowserWindow *self);
void browser_window_navigate(BrowserWindow *self, const char *uri);
void browser_window_go_back(BrowserWindow *self);
void browser_window_go_forward(BrowserWindow *self);
void browser_window_reload(BrowserWindow *self);
void browser_window_stop_loading(BrowserWindow *self);
void browser_window_toggle_fullscreen(BrowserWindow *self);
void browser_window_find_in_page(BrowserWindow *self, const char *text);
void browser_window_zoom_in(BrowserWindow *self);
void browser_window_zoom_out(BrowserWindow *self);
void browser_window_zoom_reset(BrowserWindow *self);
void browser_window_toggle_sidebar(BrowserWindow *self);
void browser_window_new_private_tab(BrowserWindow *self);
void browser_window_pin_tab(BrowserWindow *self, int index);
void browser_window_duplicate_tab(BrowserWindow *self, int index);
void browser_window_mute_tab(BrowserWindow *self, int index);

G_END_DECLS

#endif /* BROWSER_WINDOW_H */
