#include "http.h"

#include <fcntl.h>
#include <errno.h>

#include "log.h"
#include "utils.h"

void http_debug (const Http* http);

Http *http_new(GUri* uri)
{
    g_return_val_if_fail (uri, NULL);

    Http* http = g_malloc0 (sizeof (Http));
    if (!http) {
        goto error;
    }

    // init size
    http->headerBufLen = 1024;

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
    if (http->error)                g_error_free (http->error);
//    if (http->bodyBuf)              g_free (http->bodyBuf);

    g_free (http);
}

bool http_request(Http *http, const char* fileName)
{
    g_return_val_if_fail (http && fileName, false);

    // get request header
    g_autofree char* req =  http_request_get_string (http->request);
    if (!req) {
        gf_error (&http->error, "http request get header error");
        return false;
    }

    // connect
    bool useSSL = !g_ascii_strcasecmp (http->schema, "https") ? true : false;
    if (!tcp_connect (http->tcp, http->host, http->port, useSSL, NULL, -1)) {
        gf_error (&http->error, http->tcp->error->message);
        return false;
    }

    logd ("\n================ request ===================\n"
          "%s"
          "\n============================================\n", req);

    // send request
    if (tcp_write (http->tcp, req, strlen (req)) < 0) {
        gf_error (&http->error, "tcp write return false");
        return false;
    }

    // read header
    int step = 1;
    if (!(http->headerBuf = g_malloc0 (http->headerBufLen))) {
        gf_error (&http->error, "http malloc header buf fail!");
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
                gf_error (&http->error, "g_realloc header buf failed");
                return false;
            }
            ++step;
            http->headerBufLen = len;
            http->headerBuf = t;
        }
    }
    http->headerBuf[http->headerBufCurLen] = 0;
    logd ("read header OK!");

    logd ("\n================ respose ===================\n"
         "%s"
         "\n============================================\n", http->headerBuf);

    // parse header

    // Multithreaded download

    // read body
    int ret = 0;
    char buf[1024] = {0};

    g_autofree char* fileT = NULL;
    if ('/' == fileName[0]) {
        fileT = g_strdup (fileName);
    } else {
        const char* dir = g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD);
        g_return_val_if_fail (dir, false);
        fileT = g_strdup_printf ("%s/%s", dir, fileName);
    }

    printf ( "%s\n", fileT);

    g_autoptr (GError) error = NULL;
    g_autoptr (GFile) file = g_file_new_for_path (fileT);
    if (G_IS_FILE (file)) {
        if (g_file_query_exists (file, NULL)) {
            GFileType type = g_file_query_file_type (file, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL);
            if (G_FILE_TYPE_DIRECTORY != type) {
                gf_error (&http->error, "file '%s' already exists!", fileT, NULL);
                return false;
            }
        }
    }

    // permission can open? and write?
    int fd = open (fileT, O_CREAT | O_RDWR, 0777);
    if (fd < 0) {
        gf_error (&http->error, "fail to open '%s', error: %s", fileT, strerror (errno), NULL);
        goto error;
    }

    for (int i = 0;; ++i) {
        memset (buf, 0 , sizeof (buf));
        ret = tcp_read (http->tcp, buf, sizeof (buf) - 1);
        if (ret <= 0) {
            logd ("http read OK");
            break;
        }

        if (write (fd, buf, ret) < 0) {
            gf_error (&http->error, "http download error: %s", strerror (errno), NULL);
            goto error;
        }
    }

    close (fd);

    return true;

error:
    if (fd > 0)     close (fd);

    return false;
}


void http_debug (const Http* http)
{
    g_return_if_fail (http);

    printf ("=========================== http ==========================\n");
    printf ("host: %s\n", http->host);
    printf ("resource: %s\n", http->resource);
    if (http->headerBuf)    printf ("\nheader ==>\n%s\n===\n", http->headerBuf);
    printf ("===========================================================\n");
}
