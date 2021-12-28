#include "http-request.h"

#include "tcp.h"

#ifndef VERSION
const char *gVersionAgent = "graceful downloader version:no define";
#else
const char *gVersionAgent = TARGET_NAME " version " VERSION;
#endif


const char* gHttpRequestTypeStr[] = {
    "GET",
    "OPTIONS",
    "HEAD",
    "POST",
    "PUT",
    "DELETE",
    "TRACE",
    "CONNECT",
    "PROPFIND",
    "PROPPATCH",
    "MKCOL",
    "COPY",
    "MOVE",
    "LOCK",
    "UNLOCK",
    NULL
};


HttpRequest *http_request_new (const char* host, const char* resource)
{
    HttpRequest* req = g_malloc0 (sizeof (HttpRequest));
    if (!req) {
        goto error;
    }

    req->host = g_strdup (host);
    if (!req->host) {
        goto error;
    }

    req->resource = g_strdup (resource);
    if (!req->resource) {
        goto error;
    }

    req->httpVer = 1.1;

    req->headers = http_header_list_new ();
    if (!req->headers) {
        goto error;
    }

    http_header_list_set_value (req->headers, gHttpHeaderHost, req->host);
    http_header_list_set_value (req->headers, gHttpHeaderAccept, "*/*");
    http_header_list_set_value (req->headers, gHttpHeaderUserAgent, gVersionAgent);
    http_header_list_set_value (req->headers, gHttpHeaderAcceptEncoding, "identity");

    return req;

error:
    if (req && req->host)           {g_free (req->host);     req->host = NULL;}
    if (req && req->resource)       {g_free (req->resource); req->resource = NULL;}

    http_request_destroy (req);

    return NULL;
}

void http_request_destroy (HttpRequest *req)
{
    g_return_if_fail (req);

    if (req->host)           g_free (req->host);
    if (req->resource)       g_free (req->resource);

    if (req->headers) http_header_list_destroy (req->headers);

    g_free (req);
}

// 30
char *http_request_get_string (HttpRequest *req)
{
    g_return_val_if_fail (req && req->host && req->resource && req->headers, NULL);


    const ssize_t lineAddLen = 6;
    const ssize_t minLen = 32;

    size_t reqCurLen = 0;
    size_t reqLen = 1024;

    size_t resLen = strlen (req->resource);

    if (minLen + resLen > reqLen) {
        reqLen += (minLen + resLen);
    }

    char* reqStr = g_malloc0 (reqLen + 1);
    if (!reqStr) {
        goto error;
    }

    reqCurLen = g_snprintf (reqStr, reqLen, "%s %s HTTP/%01.1f\r\n", gHttpRequestTypeStr[req->type], req->resource, req->httpVer);

    // some other fields
    int kl = 0, vl = 0, remLen = 0, lineLen = 0, ret = 0;
    for (int i = 0; i < HTTP_HEADER_MAX; ++i) {
        if (!req->headers->value[i] || !req->headers->header[i]) continue;
        kl = strlen (req->headers->value[i]);
        vl = strlen (req->headers->header[i]);
        if ((kl > 0) && (vl > 0)) {
            remLen = reqLen - reqCurLen - 10;
            lineLen = kl + vl + lineAddLen;
            if (lineLen > remLen) {
                reqStr = g_realloc (reqStr, reqLen + lineLen - remLen + 1);
                if (reqStr) goto error;
            }

            ret = g_snprintf (reqStr + reqCurLen, reqLen - reqCurLen, "%s: %s\r\n", req->headers->header[i], req->headers->value[i]);
            reqCurLen += ret;
        }
    }
    reqStr[reqCurLen] = '\r';
    reqStr[++reqCurLen] = '\n';
    reqStr[++reqCurLen] = '\0';

    return reqStr;

error:
    if (reqStr) g_free (reqStr);

    return NULL;
}
