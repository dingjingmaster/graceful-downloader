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
    char*                   outputDir;
    char*                   outputName;

    /**
     * @TODO read and write lock for progress
     */
    void*                   data;

    //
    Conf*                   conf;
    double                  startTime;
    int                     nextState, finishTime;
    off_t                   bytesDone, startByte, size;
    long long int           bytesPerSecond;
    struct timespec         delayTime;

    int                     outfd;
    int                     ready;

    char*                   buf;

//    Message                 *message, *lastMessage;
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
