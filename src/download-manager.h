#ifndef DOWNLOAD_MANAGER_H
#define DOWNLOAD_MANAGER_H

#include <adwaita.h>
#include <webkit/webkit.h>

G_BEGIN_DECLS

#define DOWNLOAD_TYPE_MANAGER (download_manager_get_type())
G_DECLARE_FINAL_TYPE(DownloadManager, download_manager, DOWNLOAD, MANAGER, GObject)

DownloadManager *download_manager_get_default(void);
void download_manager_add_download(DownloadManager *self, WebKitDownload *download);
void download_manager_pause(DownloadManager *self, int index);
void download_manager_resume(DownloadManager *self, int index);
void download_manager_cancel(DownloadManager *self, int index);
void download_manager_retry(DownloadManager *self, int index);
void download_manager_open_file(DownloadManager *self, int index);
void download_manager_open_folder(DownloadManager *self, int index);
void download_manager_clear_completed(DownloadManager *self);
GListModel *download_manager_get_downloads(DownloadManager *self);
GtkWidget *download_manager_create_widget(DownloadManager *self);
GtkWidget *download_manager_create_full_page(DownloadManager *self);

G_END_DECLS

#endif /* DOWNLOAD_MANAGER_H */
