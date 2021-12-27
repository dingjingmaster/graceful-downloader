#ifndef HTTPRESPOSE_H
#define HTTPRESPOSE_H

#include <gio/gio.h>
#include <stdbool.h>

#include "http-header.h"


typedef struct _HttpRespose         HttpResponse;


struct _HttpRespose
{
    float               httpVersion;
    int                 statusCode;
    char               *reason;
    HttpHeaderList     *headers;
    char               *body;
    int                 bodyLen;
};


HttpResponse*   http_respose_new ();
void            http_respose_destroy (HttpResponse* resp);


#endif // HTTPRESPOSE_H
