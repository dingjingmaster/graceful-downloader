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

int http_connect (Http *conn, int proto, char *proxy, char *host, int port, char *user, char *pass, unsigned io_timeout)
{
    const char *puser = NULL, *ppass = "";
    Conn tconn[1];

    gf_strlcpy(conn->host, host, sizeof(conn->host));
    conn->port = port;
    conn->proto = proto;

    if (proxy && *proxy) {
        if (!conn_set(tconn, proxy)) {
            loge ("Invalid proxy string: %s", proxy);
            return 0;
        }
        host = tconn->host;
        port = tconn->port;
        proto = tconn->proto;
        puser = tconn->user;
        ppass = tconn->pass;
        conn->proxy = 1;
    }

    if (tcp_connect(&conn->tcp, host, port, PROTO_IS_SECURE(proto),
            conn->local_if, io_timeout) == -1)
        return 0;

    if (*user == 0) {
        *conn->auth = 0;
    } else {
        http_auth_token(conn->auth, user, pass);
    }

    if (!conn->proxy || !puser || *puser == 0) {
        *conn->proxy_auth = 0;
    } else {
        http_auth_token(conn->proxy_auth, puser, ppass);
    }

    return 1;
}

void http_disconnect(Http *conn)
{
    tcp_close(&conn->tcp);
}

void http_get(Http *conn, char *lurl)
{
    const char *prefix = "", *postfix = "";

       // If host is ipv6 literal add square brackets
    if (is_ipv6_addr(conn->host)) {
        prefix = "[";
        postfix = "]";
    }

    *conn->request->p = 0;
    if (conn->proxy) {
        const char *proto = scheme_from_proto(conn->proto);
        if (is_default_port(conn->proto, conn->port)) {
            http_addheader(conn, "GET %s%s%s%s%s HTTP/1.0", proto, prefix, conn->host, postfix, lurl);
        } else {
            http_addheader(conn, "GET %s%s%s%s:%i%s HTTP/1.0", proto, prefix, conn->host, postfix, conn->port, lurl);
        }
    } else {
        http_addheader(conn, "GET %s HTTP/1.0", lurl);
        if (is_default_port(conn->proto, conn->port)) {
            http_addheader(conn, "Host: %s%s%s", prefix, conn->host, postfix);
        } else {
            http_addheader(conn, "Host: %s%s%s:%i", prefix, conn->host, postfix, conn->port);
        }
    }
    if (*conn->auth) {
        http_addheader(conn, "Authorization: Basic %s", conn->auth);
    }

    if (*conn->proxy_auth) {
        http_addheader(conn, "Proxy-Authorization: Basic %s", conn->proxy_auth);
    }

    http_addheader(conn, "Accept: */*");
    http_addheader(conn, "Accept-Encoding: identity");

    if (conn->lastbyte && conn->firstbyte >= 0) {
        http_addheader(conn, "Range: bytes=%jd-%jd", conn->firstbyte, conn->lastbyte - 1);
    } else if (conn->firstbyte >= 0) {
        http_addheader(conn, "Range: bytes=%jd-", conn->firstbyte);
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

int http_exec(Http *conn)
{
    char *s2;

    logd ("--- Sending request ---\n%s\n--- End of request ---\n", conn->request->p);

    gf_strlcat(conn->request->p, "\r\n", conn->request->len);

    const size_t reqlen = strlen(conn->request->p);
    size_t nwrite = 0;
    while (nwrite < reqlen) {
        ssize_t tmp;
        tmp = tcp_write(&conn->tcp, conn->request->p + nwrite, reqlen - nwrite);
        if (tmp < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }

            loge ("Connection gone while writing.");

            return 0;
        }
        nwrite += tmp;
    }

    *conn->headers->p = 0;

    /* Read the headers byte by byte to make sure we don't touch the
       actual data */
    for (char *s = conn->headers->p;;) {
        if (tcp_read(&conn->tcp, s, 1) <= 0) {
            loge ("Connection gone.");

            return 0;
        }

        if (*s == '\r') {
            continue;
        } else if (*s == '\n') {
            if (s > conn->headers->p && s[-1] == '\n') {
                *s = 0;
                break;
            }
        }

        s++;

        size_t pos = s - conn->headers->p;
        if (pos + 10 < conn->headers->len) {
            int tmp = abuf_setup(conn->headers, conn->headers->len + HDR_CHUNK);
            if (tmp < 0) {
                loge ("Out of memory");
                return 0;
            }
            s = conn->headers->p + pos;
        }
    }

    logd ("--- Reply headers ---\n%s\n--- End of headers ---\n", conn->headers->p);

    sscanf(conn->headers->p, "%*s %3i", &conn->status);
    s2 = strchr (conn->headers->p, '\n');
    if (s2) {
        *s2 = 0;
    }

    const size_t reslen = s2 - conn->headers->p + 1;
    if (conn->request->len < reqlen) {
        int ret = abuf_setup(conn->request, reslen);
        if (ret < 0)
            return 0;
    }

    memcpy(conn->request->p, conn->headers->p, reslen);
    *s2 = '\n';

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
void http_filename(const Http *conn, char *filename)
{
    const char *h;
    if ((h = http_header(conn, "Content-Disposition:")) != NULL) {
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
