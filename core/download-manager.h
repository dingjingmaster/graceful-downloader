#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H
#include <stdbool.h>
#include <gio/gio.h>

typedef struct _DownloadData        DownloadData;


struct _DownloadData
{
    GList*                  uris;
    char*                   outputDir;
    char*                   outputName;

    /**
     * @TODO read and write lock for progress
     */
};


bool protocol_register ();
void protocol_unregister ();
GUri* url_Analysis (const char* url);
static void* download (DownloadData* data);


#endif // DOWNLOADMANAGER_H
