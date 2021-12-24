#include "http-header.h"

#include <gio/gio.h>

/* entity headers */
const char gHttpHeaderAllow[]               = "Allow";
const char gHttpHeaderContentEncoding[]     = "Content-Encoding";
const char gHttpHeaderContentLanguage[]     = "Content-Language";
const char gHttpHeaderContentLength[]       = "Content-Length";
const char gHttpHeaderContentLocation[]     = "Content-Location";
const char gHttpHeaderContentMD5[]          = "Content-MD5";
const char gHttpHeaderContentRange[]        = "Content-Range";
const char gHttpHeaderContentType[]         = "Content-Type";
const char gHttpHeaderExpires[]             = "Expires";
const char gHttpHeaderLastModified[]        = "Last-Modified";

/* general headers */
const char gHttpHeaderCacheControl[]        = "Cache-Control";
const char gHttpHeaderConnection[]          = "Connection";
const char gHttpHeaderDate[]                = "Date";
const char gHttpHeaderPragma[]              = "Pragma";
const char gHttpHeaderTransferEncoding[]    = "Transfer-Encoding";
const char gHttpHeaderUpdate[]              = "Update";
const char gHttpHeaderTrailer[]             = "Trailer";
const char gHttpHeaderVia[]                 = "Via";

/* request headers */
const char gHttpHeaderAccept[]              = "Accept";
const char gHttpHeaderAcceptCharset[]       = "Accept-Charset";
const char gHttpHeaderAcceptEncoding[]      = "Accept-Encoding";
const char gHttpHeaderAcceptLanguage[]      = "Accept-Language";
const char gHttpHeaderAuthorization[]       = "Authorization";
const char gHttpHeaderExpect[]              = "Expect";
const char gHttpHeaderFrom[]                = "From";
const char gHttpHeaderHost[]                = "Host";
const char gHttpHeaderIfModifiedSince[]     = "If-Modified-Since";
const char gHttpHeaderIfMatch[]             = "If-Match";
const char gHttpHeaderIfNoneMatch[]         = "If-None-Match";
const char gHttpHeaderIfRange[]             = "If-Range";
const char gHttpHeaderIfUnmodifiedSince[]   = "If-Unmodified-Since";
const char gHttpHeaderMaxForwards[]         = "Max-Forwards";
const char gHttpHeaderProxyAuthorization[]  = "Proxy-Authorization";
const char gHttpHeaderRange[]               = "Range";
const char gHttpHeaderReferrer[]            = "Referrer";
const char gHttpHeaderTE[]                  = "TE";
const char gHttpHeaderUserAgent[]           = "User-Agent";


/* response headers */
const char gHttpHeaderAcceptRanges[]        = "Accept-Ranges";
const char gHttpHeaderAge[]                 = "Age";
const char gHttpHeaderETag[]                = "ETag";
const char gHttpHeaderLocation[]            = "Location";
const char gHttpHeaderRetryAfter[]          = "Retry-After";
const char gHttpHeaderServer[]              = "Server";
const char gHttpHeaderVary[]                = "Vary";
const char gHttpHeaderWarning[]             = "Warning";
const char gHttpHeaderWWWAuthenticate[]     = "WWW-Authenticate";

/* Other headers */
const char gHttpHeaderSetCookie[]           = "Set-Cookie";

/* WebDAV headers */
const char gHttpHeaderDAV[]                 = "DAV";
const char gHttpHeaderDepth[]               = "Depth";
const char gHttpHeaderDestination[]         = "Destination";
const char gHttpHeaderIf[]                  = "If";
const char gHttpHeaderLockToken[]           = "Lock-Token";
const char gHttpHeaderOverwrite[]           = "Overwrite";
const char gHttpHeaderStatusURI[]           = "Status-URI";
const char gHttpHeaderTimeout[]             = "Timeout";

const char* gHttpHeaderKnownList [] = {
    /* entity headers */
    gHttpHeaderAllow,
    gHttpHeaderContentEncoding,
    gHttpHeaderContentLanguage,
    gHttpHeaderContentLength,
    gHttpHeaderContentLocation,
    gHttpHeaderContentMD5,
    gHttpHeaderContentRange,
    gHttpHeaderContentType,
    gHttpHeaderExpires,
    gHttpHeaderLastModified,
    /* general headers */
    gHttpHeaderCacheControl,
    gHttpHeaderConnection,
    gHttpHeaderDate,
    gHttpHeaderPragma,
    gHttpHeaderTransferEncoding,
    gHttpHeaderUpdate,
    gHttpHeaderTrailer,
    gHttpHeaderVia,
    /* request headers */
    gHttpHeaderAccept,
    gHttpHeaderAcceptCharset,
    gHttpHeaderAcceptEncoding,
    gHttpHeaderAcceptLanguage,
    gHttpHeaderAuthorization,
    gHttpHeaderExpect,
    gHttpHeaderFrom,
    gHttpHeaderHost,
    gHttpHeaderIfModifiedSince,
    gHttpHeaderIfMatch,
    gHttpHeaderIfNoneMatch,
    gHttpHeaderIfRange,
    gHttpHeaderIfUnmodifiedSince,
    gHttpHeaderMaxForwards,
    gHttpHeaderProxyAuthorization,
    gHttpHeaderRange,
    gHttpHeaderReferrer,
    gHttpHeaderTE,
    gHttpHeaderUserAgent,
    /* response headers */
    gHttpHeaderAcceptRanges,
    gHttpHeaderAge,
    gHttpHeaderETag,
    gHttpHeaderLocation,
    gHttpHeaderRetryAfter,
    gHttpHeaderServer,
    gHttpHeaderVary,
    gHttpHeaderWarning,
    gHttpHeaderWWWAuthenticate,
    NULL,
};


HttpHeaderList *http_header_list_new ()
{
    return g_malloc0 (sizeof (HttpHeaderList));
}

void http_header_list_destroy(HttpHeaderList *ls)
{
    g_return_if_fail (ls);

    for (int i = 0; i < HTTP_HEADER_MAX; ++i) {
        if (ls->header[i] && (NULL == http_header_is_known (ls->header[i]))) {
            g_free (ls->header[i]);
        }

        if (ls->value[i]) {
            g_free (ls->value[i]);
        }
    }

    g_free (ls);
}

const char *http_header_is_known(const char *hKey)
{
    g_return_val_if_fail (hKey, NULL);

    int i = 0;
    while (NULL != gHttpHeaderKnownList[i]) {
        if (!g_ascii_strcasecmp (hKey, gHttpHeaderKnownList[i])) {
            return gHttpHeaderKnownList[i];
        }
        ++i;
    }

    return NULL;
}

bool http_header_list_set_value(HttpHeaderList *ls, const char *key, const char *value)
{
    g_return_val_if_fail (ls && key && value, false);

    char* tmpVal = http_header_list_get_value (ls, key);
    if (!tmpVal) {
        for (int i = 0; i < HTTP_HEADER_MAX; ++i) {
            if (!ls->header[i]) {
                tmpVal = (char*) http_header_is_known (key);
                if (tmpVal) {
                    ls->header[i] = tmpVal;
                } else {
                    ls->header[i] = g_strdup (key);
                }

                ls->value[i] = g_strdup (value);
                return true;
            }
        }
    } else {
        for (int i = 0; i < HTTP_HEADER_MAX; ++i) {
            if (ls->value[i] == tmpVal) {
                g_free (ls->value[i]);
                ls->value[i] = g_strdup (value);
                return true;
            }
        }
    }

    return false;
}

char *http_header_list_get_value(HttpHeaderList *ls, const char *key)
{
    g_return_val_if_fail (ls && key, false);

    for (int i = 0; i < HTTP_HEADER_MAX; ++i) {
        if (ls->header[i] && !g_ascii_strcasecmp (ls->header[i], key)) {
            if (!ls->value[i]) {
                break;
            }
            return ls->value[i];
        }
    }

    return NULL;
}

bool http_header_list_get_headers(HttpHeaderList *ls, char ***names, int *numNames)
{
    g_return_val_if_fail (ls && names && numNames, false);

    int lNumNames = 0;
    for (int i = 0; i < HTTP_HEADER_MAX; ++i) {
        if (ls->header[i]) {
            ++lNumNames;
        }
    }

    if (0 == lNumNames) return true;

    char** lNames = g_malloc0 (sizeof (char*) * lNumNames);
    if (!lNames) {
        goto error;
    }

    for (int i = 0; i < HTTP_HEADER_MAX; ++i) {
        if (ls->header[i]) {
            lNames[i] = g_strdup (ls->header[i]);
            if (!lNames[i]) {
                goto error;
            }
        }
    }

    *names = lNames;
    *numNames = lNumNames;

    return true;

error:
    if (lNames) {
        for (int i = 0; i < lNumNames; ++i) {
            if (lNames[i]) {
                g_free (lNames[i]);
                lNames[i] = 0;
            }
        }

        g_free (lNames);
        *names = NULL;
    }

    *numNames = 0;

    return false;
}

bool http_header_clear_value(HttpHeaderList *ls, const char *name)
{
    g_return_val_if_fail (ls && name, false);

    for (int i = 0; i < HTTP_HEADER_MAX; ++i) {
        if (ls->header[i] && !g_ascii_strcasecmp (ls->header[i], name)) {
            if (!http_header_is_known (name)) {
                g_free (ls->header[i]);
            }

            ls->header[i] = NULL;
            g_free (ls->value[i]);
            ls->value[i] = NULL;
        }
    }

    return true;
}

