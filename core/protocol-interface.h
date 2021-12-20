#ifndef PROTOCOLINTERFACE_H
#define PROTOCOLINTERFACE_H

#include <stdbool.h>
#include <gio/gio.h>

typedef struct _Downloader          Downloader;
typedef struct _DownloadData        DownloadData;
typedef struct _DownloadMethod      DownloadMethod;

typedef bool (*Init)        (Downloader* data);
typedef bool (*Download)    (Downloader* data);
typedef void (*Free)        (Downloader* data);

struct _DownloadData
{
    GUri*                   uri;
    char*                   outputDir;
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
    DownloadMethod*         method;

    DownloadData*           data;
};


#endif // PROTOCOLINTERFACE_H
