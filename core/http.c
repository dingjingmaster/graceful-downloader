#include "http.h"

#include "log.h"


void http_debug (Http* http);

Http *http_new(GUri* uri)
{
    g_return_val_if_fail (uri, NULL);

    Http* http = g_malloc0 (sizeof (Http));
    if (!http) {
        goto error;
    }

    // init size
    http->headerBufLen = 1024;
    http->bodyBufLen = 10240;

    // port
    const char* schema = g_uri_get_scheme (uri);
    if (0 == g_ascii_strcasecmp (schema, "https")) {
        http->port = 443;
        http->schema = g_strdup ("https");
    } else {
        http->port = 80;
        http->schema = g_strdup ("http");
    }

    int port = g_uri_get_port (uri);
    if (port > 0) {
        http->port = port;
    }

    // not http or https
    if (http->port <= 0)                        goto error;

    // host
    const char* host = g_uri_get_host (uri);
    if (!host)                                  goto error;
    http->host = g_strdup (host);

    // resource
    const char* path = g_uri_get_path (uri);
    if (!path) {
        path = "/index.html";
    }
    http->resource = g_strdup (path);


    if (!(http->tcp = tcp_new ()))              goto error;
    if (!(http->resp = http_respose_new ()))    goto error;
    if (!(http->request = http_request_new (http->host, http->resource)))   goto error;

    logd ("\n============= create new http request ================\n"
         "host: %s\npath:%s\n"
         "=====================================================", host, path);

    return http;

error:
    if (http)                       http_destroy (http);

    return NULL;
}

void http_destroy(Http *http)
{
    g_return_if_fail (http);

    if (http->schema)               g_free (http->schema);
    if (http->host)                 g_free (http->host);
    if (http->resource)             g_free (http->resource);
    if (http->tcp)                  tcp_destroy (&http->tcp);
    if (http->resp)                 http_respose_destroy (http->resp);
    if (http->request)              http_request_destroy (http->request);
    if (http->headerBuf)            g_free (http->headerBuf);
    if (http->bodyBuf)              g_free (http->bodyBuf);

    g_free (http);
}

bool http_request(Http *http)
{
    g_return_val_if_fail (http, false);

    // get request header
    g_autofree char* req =  http_request_get_string (http->request);
    if (!req) {
        logd ("http request get header error");
        return false;
    }

    // connect
    bool useSSL = !g_ascii_strcasecmp (http->schema, "https") ? true : false;
    if (!tcp_connect (http->tcp, http->host, http->port, useSSL, NULL, -1)) {
        logd ("tcp_connect error: %s", http->tcp->error->message);
        return false;
    }

    logd ("\n================ request ===================\n"
          "%s"
          "\n============================================\n", req);

    // send request
    if (tcp_write (http->tcp, req, strlen (req)) < 0) {
        logd ("tcp write return false");
        return false;
    }

    // read header
    int step = 1;
    if (!(http->headerBuf = g_malloc0 (http->headerBufLen))) {
        logd ("http malloc header buf fail!");
        return false;
    }

    for (http->headerBufCurLen = 0;;) {
        if (tcp_read (http->tcp, &http->headerBuf[http->headerBufCurLen], 1) <= 0) {
            logd ("http read OK");
            break;
        }

        if ('\r' == http->headerBuf[http->headerBufCurLen]) {
            continue;
        } else if (('\n' == http->headerBuf[http->headerBufCurLen])
            && (http->headerBufCurLen > 0) && ('\n' == http->headerBuf[http->headerBufCurLen - 1])) {
            logd ("http read header OK");
            break;
        }
        ++http->headerBufCurLen;

        if (http->headerBufCurLen + 10 < http->headerBufLen) {
            int len = http->headerBufLen + step * 512;
            char* t = g_realloc (http->headerBuf, len);
            if (!t) {
                logd ("g_realloc header buf failed");
                return false;
            }
            ++step;
            http->headerBufLen = len;
            http->headerBuf = t;
        }
    }
    http->headerBuf[http->headerBufCurLen] = 0;
    logd ("read header OK!");

    // parse header

    // if read body???

    // read body
    int ret = 0;
    char buf[1024] = {0};
    if (!(http->bodyBuf = g_malloc0 (http->bodyBufLen))) {
        logd ("http malloc body buf fail!");
        return false;
    }

    for (int i = 0;; ++i) {
        memset (buf, 0 , sizeof (buf));
        ret = tcp_read (http->tcp, buf, sizeof (buf) - 1);
        if (ret <= 0) {
            logd ("http read OK");
            break;
        }

        if (http->bodyBufCurLen + ret + 1 < http->bodyBufLen) {
            int len = http->bodyBufLen + sizeof (buf);
            char* t = g_realloc (http->bodyBuf, len);
            if (!t) {
                logd ("g_realloc body buf failed");
                return false;
            }
            http->bodyBufLen = len;
            http->bodyBuf = t;
        }
        memccpy (http->bodyBuf + http->bodyBufCurLen, buf, 0, ret);
        http->bodyBufCurLen += ret;
    }
    http->bodyBuf[http->bodyBufCurLen] = 0;

    return true;
}


void http_debug (Http* http)
{
    g_return_if_fail (http);

    printf ("=========================== http ==========================\n");
    printf ("host: %s\n", http->host);
    printf ("resource: %s\n", http->resource);
    if (http->headerBuf)    printf ("\nheader ==>\n%s\n===\n", http->headerBuf);
    if (http->bodyBuf)      printf ("\nbody ==>\n%s\n===\n", http->bodyBuf);
    printf ("===========================================================\n");
}
