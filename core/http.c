#include "http.h"

#include <errno.h>

#include "log.h"
#include "conn.h"
#include "config.h"
#include "utils.h"


#define HDR_CHUNK 512



inline static int is_default_port (int proto, int port)
{
    return ((proto == PROTO_HTTP && port == PROTO_HTTP_PORT) || (proto == PROTO_HTTPS && port == PROTO_HTTPS_PORT));
}

inline static char chain_next (const char ***p)
{
    while (**p && !***p) {
        ++(*p);
    }

    return **p ? *(**p)++ : 0;
}

static void http_auth_token (char *token, const char *user, const char *pass)
{
    const char base64_encode[64] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz" "0123456789+/";

    const char *auth[] = { user, ":", pass, NULL };
    const char **p = auth;

    while (*p) {
        char a = chain_next(&p);
        if (!a)
            break;
        *token++ = base64_encode[a >> 2];
        char b = chain_next(&p);
        *token++ = base64_encode[((a & 3) << 4) | (b >> 4)];
        if (!b) {
            *token++ = '=';
            *token++ = '=';
            break;
        } else {
            char c = chain_next(&p);
            *token++ = base64_encode[((b & 15) << 2) | (c >> 6)];
            if (!c) {
                *token++ = '=';
                break;
            } else {
                *token++ = base64_encode[c & 63];
            }
        }
    }
}

int http_connect (Http* http, int proto, const char *proxy, const char *host, int port, const char *user, const char *pass, unsigned ioTimeout)
{
    const char *puser = NULL, *ppass = "";
//    Conn tconn[1];

    gf_strlcpy(http->host, host, sizeof(http->host));
    http->port = port;
    http->proto = proto;

//    if (proxy && *proxy) {
//        if (!conn_set(tconn, proxy)) {
//            loge ("Invalid proxy string: %s", proxy);
//            return 0;
//        }
//        host = tconn->host;
//        port = tconn->port;
//        proto = tconn->proto;
//        puser = tconn->user;
//        ppass = tconn->pass;
//        http->proxy = 1;
//    }

    if (tcp_connect(&http->tcp, host, port, PROTO_IS_SECURE(proto), http->localIf, ioTimeout) == -1)
        return 0;

    if (NULL == user) {
        *http->auth = 0;
    } else {
        http_auth_token(http->auth, user, pass);
    }

    if (!http->proxy || !puser || *puser == 0) {
        *http->proxy_auth = 0;
    } else {
        http_auth_token(http->proxy_auth, puser, ppass);
    }

    return 1;
}

void http_disconnect(Http *http)
{
    tcp_close(&http->tcp);
}

void http_get(Http *http, const char *lurl)
{
    const char *prefix = "", *postfix = "";

       // If host is ipv6 literal add square brackets
    if (is_ipv6_addr(http->host)) {
        prefix = "[";
        postfix = "]";
    }

    *http->request->p = 0;
    if (http->proxy) {
        const char *proto = scheme_from_proto(http->proto);
        if (is_default_port(http->proto, http->port)) {
            http_addheader(http, "GET %s%s%s%s%s HTTP/1.0", proto, prefix, http->host, postfix, lurl);
        } else {
            http_addheader(http, "GET %s%s%s%s:%i%s HTTP/1.0", proto, prefix, http->host, postfix, http->port, lurl);
        }
    } else {
        http_addheader (http, "GET %s HTTP/1.0", lurl);
        if (is_default_port (http->proto, http->port)) {
            http_addheader(http, "Host: %s%s%s", prefix, http->host, postfix);
        } else {
            http_addheader(http, "Host: %s%s%s:%i", prefix, http->host, postfix, http->port);
        }
    }
    if (*http->auth) {
        http_addheader(http, "Authorization: Basic %s", http->auth);
    }

    if (*http->proxy_auth) {
        http_addheader(http, "Proxy-Authorization: Basic %s", http->proxy_auth);
    }

    http_addheader(http, "Accept: */*");
    http_addheader(http, "Accept-Encoding: identity");

    // 此处可以打开多线程下载
    if (http->lastbyte && http->firstbyte >= 0) {
        http_addheader(http, "Range: bytes=%jd-%jd", http->firstbyte, http->lastbyte - 1);
    } else if (http->firstbyte >= 0) {
        http_addheader(http, "Range: bytes=%jd-", http->firstbyte);
    }
}

void http_addheader(Http *conn, const char *format, ...)
{
    char s[MAX_STRING];
    va_list params;

    va_start(params, format);
    vsnprintf(s, sizeof(s) - 3, format, params);
    gf_strlcat(s, "\r\n", sizeof(s));
    va_end(params);

    if (abuf_strcat(conn->request, s) < 0) {
        loge ("Out of memory");
    }
}

int http_exec(Http *http)
{
    char *s2;

    logd ("\n--- Sending request ---\n%s\n--- End of request ---\n", http->request->p);

    gf_strlcat(http->request->p, "\r\n", http->request->len);

    const size_t reqlen = strlen(http->request->p);
    size_t nwrite = 0;
    while (nwrite < reqlen) {
        ssize_t tmp;
        tmp = tcp_write(&http->tcp, http->request->p + nwrite, reqlen - nwrite);
        if (tmp < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            loge ("Connection gone while writing.");

            return 0;
        }
        nwrite += tmp;
    }

    *http->headers->p = 0;

    // Read the headers byte by byte to make sure we don't touch the actual data
    for (char *s = http->headers->p;;) {
        if (tcp_read(&http->tcp, s, 1) <= 0) {
            loge ("Connection gone.");
            return 0;
        }

        if (*s == '\r') {
            continue;
        } else if (*s == '\n') {
            if (s > http->headers->p && s[-1] == '\n') {
                *s = 0;
                break;
            }
        }

        s++;

        size_t pos = s - http->headers->p;
        if (pos + 10 < http->headers->len) {
            int tmp = abuf_setup (http->headers, http->headers->len + HDR_CHUNK);
            if (tmp < 0) {
                loge ("Out of memory");
                return 0;
            }
            s = http->headers->p + pos;
        }
    }

    logd ("\n--- Reply headers ---\n%s\n--- End of headers ---\n", http->headers->p);

    sscanf(http->headers->p, "%*s %3i", &http->status);
    s2 = strchr (http->headers->p, '\n');
    if (s2) {
        *s2 = 0;
    }

    const size_t reslen = s2 - http->headers->p + 1;
    if (http->request->len < reqlen) {
        int ret = abuf_setup(http->request, reslen);
        if (ret < 0)
            return 0;
    }

    memcpy(http->request->p, http->headers->p, reslen);
    if (s2)     *s2 = '\n';

    // get some information

    return 1;
}

const char* http_header(const Http *conn, const char *header)
{
    const char *p = conn->headers->p;
    size_t hlen = strlen(header);

    do {
        if (strncasecmp(p, header, hlen) == 0) {
            return p + hlen;
        }

        while (*p != '\n' && *p) {
            p++;
        }

        if (*p == '\n') {
            p++;
        }
    } while (*p);

    return NULL;
}

off_t http_size (Http *conn)
{
    const char *i;
    off_t j;

    if ((i = http_header(conn, "Content-Length:")) == NULL) {
        return -2;
    }

    sscanf(i, "%jd", &j);

    return j;
}

off_t http_size_from_range (Http *conn)
{
    const char *i;
    if ((i = http_header(conn, "Content-Range:")) == NULL) {
        return -2;
    }

    i = strchr (i, '/');
    if (!i++) {
        return -2;
    }

    off_t j = strtoll (i, NULL, 10);
    if (!j && *i != '0') {
        return -3;
    }

    return j;
}

/**
 * Extract file name from Content-Disposition HTTP header.
 *
 * Header format:
 * Content-Disposition: inline
 * Content-Disposition: attachment
 * Content-Disposition: attachment; filename="filename.jpg"
 */
void http_filename(const Http *http, char *filename)
{
    const char *h;
    if ((h = http_header(http, "Content-Disposition:")) != NULL) {
        sscanf(h, "%*s%*[  ]filename%*[  =\"\'-]%254[^\n\"\']", filename);
        /* Trim spaces at the end of string */
        const char space[] = "  ";
        for (char *n, *p = filename; (p = strpbrk(p, space)); p = n) {
            n = p + strspn(p, space);
            if (!*n) {
                *p = 0;
                break;
            }
        }

        /* Replace common invalid characters in filename
           https://en.wikipedia.org/wiki/Filename#Reserved_characters_and_words */
        const char invalid[] = "/\\?%*:|<>";
        const char replacement = '_';
        for (char *i = filename; (i = strpbrk(i, invalid)); i++) {
            *i = replacement;
        }
    }
}

inline static char decode_nibble (char n)
{
    if (n <= '9') {
        return n - '0';
    }

    if (n >= 'a') {
        n -= 'a' - 'A';
    }

    return n - 'A' + 10;
}

inline static char encode_nibble (char n)
{
    return n > 9 ? n + 'a' - 10 : n + '0';
}

inline static void encode_byte (char dst[3], char n)
{
    *dst++ = '%';
    *dst++ = encode_nibble(n >> 4);
    *dst = encode_nibble(n & 15);
}

/* Decode%20a%20file%20name */
void http_decode (char *s)
{
    for (; *s && *s != '%'; s++) ;

    if (!*s) {
        return;
    }

    char *p = s;
    do {
        if (!s[1] || !s[2]) {
            break;
        }

        *p++ = (decode_nibble(s[1]) << 4) | decode_nibble(s[2]);
        s += 3;
        while (*s && *s != '%') {
            *p++ = *s++;
        }
    } while (*s == '%');

    *p = 0;
}

void http_encode (char *s, size_t len)
{
    char t[MAX_STRING];
    unsigned i, j;

    for (i = j = 0; s[i] && j < sizeof(t) - 1; i++, j++) {
        t[j] = s[i];
        if (s[i] <= 0x20 || s[i] >= 0x7f) {
            if (j >= sizeof(t) - 3) {
                break;
            }

            encode_byte (t + j, s[i]);
            j += 2;
        }
    }

    t[j] = 0;

    gf_strlcpy(s, t, len);
}



bool http_init(DownloadData *d)
{
    g_return_val_if_fail (d, false);

    // look for IPv6 literal hostname

    // check file is exists

    Http* http = g_malloc0 (sizeof (Http));
    if (!http) {
        loge ("g_malloc0 error: %s", strerror (errno));
        return false;
    }
    d->data = http;

    // connect
    const char* schema = g_uri_get_scheme (d->uri);
    const char* host = g_uri_get_host (d->uri);

    bool isHttps = 0 & g_strcmp0 ("https", schema);

    http_connect (http, isHttps ? PROTO_HTTPS : PROTO_HTTP, NULL, host, isHttps ? PROTO_HTTPS_PORT : PROTO_HTTP_PORT, NULL, NULL, -1);

    // http header
    int ret = abuf_setup (http->headers, 1024);
    if (0 != ret) {
        http_free (d);
        loge ("abuf_setup error: %s", strerror (ret));
        return false;
    }

    ret = abuf_setup (http->request, 1024);
    if (0 != ret) {
        http_free (d);
        loge ("abuf_setup error: %s", strerror (ret));
        return false;
    }

    // http get
    const char* path = g_uri_get_path (d->uri);
    if (!path) {
        http_free (d);
        return false;
    }

    http_get (http, path);
//    http_filename (http, "aa.html");

    return true;
}

bool http_download(DownloadData *d)
{
    g_return_val_if_fail (d && d->data, false);

    if (!http_exec (d->data)) {
        return false;
    }

    // save info

    return true;
}

void http_free(DownloadData *d)
{
//    http_disconnect ();

}
