#ifndef PROTOCOLINTERFACE_H
#define PROTOCOLINTERFACE_H

#include <stdbool.h>
#include <gio/gio.h>

#include "global.h"


typedef struct _Downloader          Downloader;
typedef struct _DownloadData        DownloadData;
typedef struct _DownloadMethod      DownloadMethod;

typedef bool (*Init)        (DownloadData* data);
typedef bool (*Download)    (DownloadData* data);
typedef void (*Free)        (DownloadData* data);


struct _DownloadData
{
    GUri*                   uri;
    char*                   outputName;

    /**
     * @TODO read and write lock for progress
     */
    void*                   data;
};

struct _DownloadMethod
{
    Init                    init;
    Download                download;
    Free                    free;
};

struct _Downloader
{
    const DownloadMethod*   method;

    DownloadData*           data;
};


#endif // PROTOCOLINTERFACE_H
