#ifndef HTTPREQUEST_H
#define HTTPREQUEST_H

#include <gio/gio.h>
#include <stdbool.h>

#include "http-header.h"

enum _HttpRequestType
{
    HTTP_REQUEST_TYPE_GET = 0,
    HTTP_REQUEST_TYPE_OPTIONS,
    HTTP_REQUEST_TYPE_HEAD,
    HTTP_REQUEST_TYPE_POST,
    HTTP_REQUEST_TYPE_PUT,
    HTTP_REQUEST_TYPE_DELETE,
    HTTP_REQUEST_TYPE_TRACE,
    HTTP_REQUEST_TYPE_CONNECT,
    HTTP_REQUEST_TYPE_PROPFIND,
    HTTP_REQUEST_TYPE_PROPPATCH,
    HTTP_REQUEST_TYPE_MKCOL,
    HTTP_REQUEST_TYPE_COPY,
    HTTP_REQUEST_TYPE_MOVE,
    HTTP_REQUEST_TYPE_LOCK,
    HTTP_REQUEST_TYPE_UNLOCK,
};

typedef struct _HttpRequest     HttpRequest;
typedef enum _HttpRequestType   HttpRequestType;


struct _HttpRequest
{
    HttpRequestType         type;
    float                   httpVer;

    char                   *host;
    char                   *resource;

    HttpHeaderList         *headers;
};

HttpRequest* http_request_new   (const char* host, const char* resource);
void  http_request_destroy      (HttpRequest* req);

char* http_request_get_string   (HttpRequest* req);


#endif // HTTPREQUEST_H
