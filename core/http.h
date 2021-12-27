#ifndef HTTP_H
#define HTTP_H

#include "tcp.h"
#include "http-request.h"
#include "http-respose.h"

typedef struct _Http            Http;

struct _Http
{
    char                   *hostName;
    int                     port;
    char                   *resource;

    Tcp                    *tcp;
    HttpRequest            *request;
    HttpResponse           *resp;
};


#endif // HTTP_H
