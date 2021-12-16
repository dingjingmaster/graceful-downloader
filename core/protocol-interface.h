#ifndef PROTOCOLINTERFACE_H
#define PROTOCOLINTERFACE_H

#include <stdbool.h>

typedef struct _Downloader  Downloader;

typedef bool (*init)    (Downloader* data);
typedef bool (*run)     (Downloader* data);
typedef void (*free)    (Downloader* data);

struct _Downloader
{
    char*           schema;
    char*           url;

    int             status;
    char*           message;

    void*           data;
};


#endif // PROTOCOLINTERFACE_H
