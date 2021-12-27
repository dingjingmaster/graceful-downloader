#ifndef HTTPRESPOSE_H
#define HTTPRESPOSE_H

#include <gio/gio.h>
#include <stdbool.h>

#include "http-header.h"


typedef struct _HttpRespose         HttpResopnse;


struct _HttpRespose
{
    float               httpVersion;
    int                 statusCode;
    char               *reason;
    HttpHeaderList     *headers;
    char               *body;
    int                 bodyLen;
};


HttpResopnse*   http_respose_new ();
void            http_respose_destroy (HttpResopnse* resp);


#endif // HTTPRESPOSE_H
