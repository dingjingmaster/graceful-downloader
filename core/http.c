#include "http.h"

#include <errno.h>

#include "log.h"
#include "conn.h"
#include "utils.h"
#include "config.h"
#include "common.h"

#define HDR_CHUNK 512


inline static bool is_https         (GUri* uri);
inline static void http_error       (DownloadData* d);

static void* setup_thread           (void *c);
static int http_status_get          (Http* http);
static bool http_get_header_info    (DownloadData* d);




    //////////////

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

    if (tcp_connect(&http->tcp, host, port, PROTO_IS_SECURE(proto), http->localIf, ioTimeout) == -1) {
        return 0;
    }

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

int http_exec (Http *http)
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

off_t http_size (Http *http)
{
    const char *i;
    off_t j;

    if ((i = http_header(http, "Content-Length:")) == NULL) {
        return -2;
    }

    sscanf(i, "%jd", &j);

    return j;
}

off_t http_size_from_range (Http* http)
{
    const char *i;
    if ((i = http_header(http, "Content-Range:")) == NULL) {
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

    Conf* conf = g_realloc (d->conf, sizeof (Conf));
    if (!conf) {
        loge ("g_realloc error: %s", strerror (errno));
        return false;
    }
    d->conf = conf;

    if (!conf_init (d->conf)) {
        loge ("conf_init error");
        return false;
    }

    Http* http = g_realloc (d->data, sizeof (Http));
    if (!http) {
        loge ("g_realloc error: %s", strerror (errno));
        return false;
    }
    d->data = http;

    Conn* conn = g_realloc_n (d->conn, d->conf->num_connections, sizeof (Conn));
    if (!conn) {
        loge ("g_realloc error: %s", strerror (errno));
        return false;
    }
    d->conn = conn;

    for (int i = 0; i < d->conf->num_connections; ++i) {
        pthread_mutex_init(&d->conn[i].lock, NULL);
    }

    g_autofree char* uri = g_uri_to_string (d->uri);
    if (d->conf->max_speed > 0) {
        /* max_speed / buffer_size < .5 */
        if (16 * d->conf->max_speed / d->conf->buffer_size < 8) {
            if (d->conf->verbose >= 2) {
                logd ("%s Buffer resized for this speed.", uri);
            }
            d->conf->buffer_size = d->conf->max_speed;
        }

        uint64_t delay = 1000000000 * d->conf->buffer_size * d->conf->num_connections / d->conf->max_speed;

        d->delayTime.tv_sec  = delay / 1000000000;
        d->delayTime.tv_nsec = delay % 1000000000;
    }

    char* buf = g_realloc (d->buf, d->conf->buffer_size);
    if (!buf) {
        loge ("g_realloc error: %s", strerror (errno));
        return false;
    }
    d->buf = buf;

    d->conn[0].conf = d->conf;
    if (!conn_set (&d->conn[0], uri)) {
        loge ("could not parse URL: %s", uri);
        d->ready = -1;
        return false;
    }

    d->conn[0].localIf = d->conf->interfaces->text;
    d->conf->interfaces = d->conf->interfaces->next;

    gf_strlcpy(d->filename, d->conn[0].file, sizeof(d->filename));
    http_decode(d->filename);

    char* s = strchr(d->filename, '?');
    if (NULL != s && d->conf->strip_cgi_parameters) {
        *s = 0;
    }

    if (*d->filename == 0) {
        gf_strlcpy(d->filename, d->conf->default_filename, sizeof(d->filename));
    }

    if (d->conf->no_clobber && access (d->filename, F_OK) == 0) {
        int ret = stfile_access (d->filename, F_OK);
        if (ret) {
            logi ("File '%s' already there; not retrieving.", d->filename);
            d->ready = -1;
            return false;
        }

        logi ("Incomplete download found, ignoring no-clobber option");
    }

    int status = 0;
    do {
        if (!conn_init(&d->conn[0])) {
            d->ready = -1;
            return false;
        }

        /* This does more than just checking the file size, it all
         * depends on the protocol used. */
        status = conn_info(&d->conn[0]);
        if (!status) {
            char msg[80];
            int code = conn_info_status_get(msg, sizeof(msg), d->conn);
            loge ("ERROR %d: %s.", code, msg);
            d->ready = -1;
            return false;
        }
    } while (status == -1);

    d->size = d->conn[0].size;
    if (d->conf->verbose > 0) {
        if (d->size != LLONG_MAX) {
            g_autofree char* size = g_format_size_full (d->size, G_FORMAT_SIZE_IEC_UNITS);
            logd ("%s File size: %s (%jd bytes)", uri, size, d->size);
        } else {
            loge ("%s File size: unavailable", uri);
        }
    }

    /* Wildcards in URL --> Get complete filename */
    if (d->filename[strcspn(d->filename, "*?")])
        gf_strlcpy(d->filename, d->conn[0].file, sizeof(d->filename));

    if (*d->conn[0].outputFilename != 0) {
        gf_strlcpy(d->filename, d->conn[0].outputFilename, sizeof (d->filename));
    }

    int fd = -1;
    ssize_t nread;
    d->outfd = -1;
    if (!d->conn[0].supported) {
        logd ("%s Server unsupported, starting from scratch with one connection.", uri);
        d->conf->num_connections = 1;
        void *newConn = realloc(d->conn, sizeof(Conn));
        if (!newConn) {
            return false;
        }

        d->conn = newConn;
        download_divide (d);
    } else if ((fd = stfile_open(d->filename, O_RDONLY, 0)) != -1) {
        int oldFormat = 0;
        off_t stsize = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);

        nread = read (fd, &d->conf->num_connections, sizeof (d->conf->num_connections));
        if (nread != sizeof(d->conf->num_connections)) {
            loge ("%s.st: Error, truncated state file", d->filename);
            close(fd);
            return false;
        }

        if (d->conf->num_connections < 1) {
            loge ("Bogus number of connections stored in state file");
            close(fd);
            return false;
        }

        if (stsize < (off_t) (sizeof (d->conf->num_connections) +
                sizeof(d->bytesDone) + 2 * d->conf->num_connections * sizeof (d->conn[0].currentbyte))) {
            /* FIXME this might be wrong, the file may have been
             * truncated, we need another way to check. */
            logd ("State file has old format.");
            oldFormat = 1;
        }

        void *newConn = realloc(d->conn, sizeof(Conn) * d->conf->num_connections);
        if (!newConn) {
            close(fd);
            return false;
        }
        d->conn = newConn;

        memset(d->conn + 1, 0, sizeof(Conn) * (d->conf->num_connections - 1));

        if (oldFormat) {
            download_divide(d);
        }

        nread = read (fd, &d->bytesDone, sizeof (d->bytesDone));
        g_return_val_if_fail (nread == sizeof (d->bytesDone), false);
        for (int i = 0; i < d->conf->num_connections; ++i) {
            nread = read(fd, &d->conn[i].currentbyte, sizeof(d->conn[i].currentbyte));
            g_return_val_if_fail (nread == sizeof(d->conn[i].currentbyte), false);
            if (!oldFormat) {
                nread = read(fd, &d->conn[i].lastbyte, sizeof (d->conn[i].lastbyte));
                g_return_val_if_fail (nread == sizeof (d->conn[i].lastbyte), false);
            }
        }

        logi ("%s State file found: %jd bytes downloaded, %jd to go.", uri, d->bytesDone, d->size - d->bytesDone);

        close(fd);

        if ((d->outfd = open(d->filename, O_WRONLY, 0666)) == -1) {
            loge ("%s Error opening local file", uri);
            return false;
        }
    }

    /* If outfd == -1 we have to start from scrath now */
    if (d->outfd == -1) {
        download_divide(d);

        if ((d->outfd = open(d->filename, O_CREAT | O_WRONLY, 0666)) == -1) {
            loge ("%s Error opening local file", uri);
            return 0;
        }

        /* And check whether the filesystem can handle seeks to
           past-EOF areas.. Speeds things up. :) AFAIK this
           should just not happen: */
        if (lseek(d->outfd, d->size, SEEK_SET) == -1 &&
            d->conf->num_connections > 1) {
            /* But if the OS/fs does not allow to seek behind
               EOF, we have to fill the file with zeroes before
               starting. Slow.. */
            logi ("%i Crappy filesystem/OS.. Working around. :-(", uri);
            lseek(d->outfd, 0, SEEK_SET);
            memset(d->buf, 0, d->conf->buffer_size);
            off_t j = d->size;
            while (j > 0) {
                ssize_t nwrite;

                if ((nwrite = write(d->outfd, d->buf, min(j, d->conf->buffer_size))) < 0) {
                    if (errno == EINTR || errno == EAGAIN) {
                        continue;
                    }
                    loge ("%s Error creating local file", uri);
                    return false;
                }
                j -= nwrite;
            }
        }
    }

    for (int i = 0; i < d->conf->num_connections; ++i) {
        conn_set (&d->conn[i], uri);
        d->conn[i].localIf = d->conf->interfaces->text;
        d->conf->interfaces = d->conf->interfaces->next;
        d->conn[i].conf = d->conf;
        if (i) {
            d->conn[i].supported = true;
        }
    }

    for (int i = 0; i < d->conf->num_connections; i++) {
        if (d->conn[i].currentbyte >= d->conn[i].lastbyte) {
            pthread_mutex_lock(&d->conn[i].lock);
            reactivate_connection (d, i);
            pthread_mutex_unlock(&d->conn[i].lock);
        } else if (d->conn[i].currentbyte < d->conn[i].lastbyte) {
            if (d->conf->verbose >= 2) {
                logd ("%s Connection %i downloading from %s:%i using interface %s", uri,
                    i, d->conn[i].host, d->conn[i].port, d->conn[i].localIf);
            }

            d->conn[i].state = true;
            if (pthread_create (d->conn[i].setupThread, NULL, setup_thread, &d->conn[i]) != 0) {
                loge ("%s pthread error!!!", uri);
                d->ready = -1;
            }
        }
    }

    d->startTime = gf_gettime();
    d->ready = 0;

    return true;
}

bool http_download(DownloadData *d)
{
    g_return_val_if_fail (d && d->data, false);

    Http* http = (Http*) d->data;

    g_autofree char* uri = g_uri_to_string (d->uri);


    fd_set fds[1];
    int hifd, i;
    off_t remaining, size;
    struct timeval timeval[1];
    Url *url_ptr;
    struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000};
    unsigned long long int max_speed_ratio;

    /* Create statefile if necessary */
    if (gf_gettime() > d->nextState) {
        save_state(d);
        d->nextState = gf_gettime() + d->conf->save_state_interval;
    }

    /* Wait for data on (one of) the connections */
    FD_ZERO(fds);
    hifd = 0;
    for (i = 0; i < d->conf->num_connections; i++) {
        /* skip connection if setup thread hasn't released the lock yet */
        if (!pthread_mutex_trylock(&d->conn[i].lock)) {
            if (d->conn[i].enabled) {
                FD_SET(d->conn[i].tcp->fd, fds);
                hifd = max(hifd, d->conn[i].tcp->fd);
            }
            pthread_mutex_unlock(&d->conn[i].lock);
        }
    }

    if (hifd == 0) {
        /* No connections yet. Wait... */
        if (gf_sleep(delay) < 0) {
            loge ("%s Error while waiting for connection: %s", strerror(errno), uri);
            d->ready = -1;
            return false;
        }

        goto conn_check;
    }

    timeval->tv_sec = 0;
    timeval->tv_usec = 100000;
    if (select(hifd + 1, fds, NULL, NULL, timeval) == -1) {
        /* A select() error probably means it was interrupted
         * by a signal, or that something else's very wrong... */
        d->ready = -1;
        return false;
    }

    /* Handle connections which need attention */
    for (i = 0; i < d->conf->num_connections; i++) {
        /* skip connection if setup thread hasn't released the lock yet */
        if (pthread_mutex_trylock(&d->conn[i].lock))
            continue;

        if (!d->conn[i].enabled)
            goto next_conn;

        if (!FD_ISSET(d->conn[i].tcp->fd, fds)) {
            time_t timeout = d->conn[i].lastTransfer + d->conf->connection_timeout;
            if (gf_gettime() > timeout) {
                if (d->conf->verbose) {
                    loge ("%s Connection %i timed out", uri, i);
                }
                conn_disconnect (&d->conn[i]);
            }
            goto next_conn;
        }

        d->conn[i].lastTransfer = gf_gettime();
        size = tcp_read(d->conn[i].tcp, d->buf, d->conf->buffer_size);
        if (size == -1) {
            if (d->conf->verbose) {
                loge("%s Error on connection %i! Connection closed", uri, i);
            }
            conn_disconnect(&d->conn[i]);
            goto next_conn;
        }

        if (size == 0) {
            if (d->conf->verbose) {
                /* Only abnormal behaviour if: */
                if (d->conn[i].currentbyte < d->conn[i].lastbyte && d->size != LLONG_MAX) {
                    loge ("%s Connection %i unexpectedly closed", uri, i);
                } else {
                    logi ("%s Connection %i finished", uri, i);
                }
            }

            if (!d->conn[0].supported) {
                d->ready = 1;
            }

            conn_disconnect(&d->conn[i]);
            reactivate_connection(d, i);
            goto next_conn;
        }

        /* remaining == Bytes to go */
        remaining = d->conn[i].lastbyte - d->conn[i].currentbyte;
        if (remaining < size) {
            if (d->conf->verbose) {
                logi ("%s Connection %i finished", uri, i);
            }
            conn_disconnect(&d->conn[i]);
            size = remaining;
            /* Don't terminate, still stuff to write! */
        }

        /* This should always succeed.. */
        lseek (d->outfd, d->conn[i].currentbyte, SEEK_SET);
        if (write(d->outfd, d->buf, size) != size) {
            loge ("%s Write error!", uri);
            d->ready = -1;
            pthread_mutex_unlock(&d->conn[i].lock);
            return false;
        }

        d->conn[i].currentbyte += size;
        d->bytesDone += size;
        if (remaining == size) {
            reactivate_connection(d, i);
        }

    next_conn:
        pthread_mutex_unlock(&d->conn[i].lock);
    }

    if (d->ready) {
        return true;
    }

conn_check:
    /* Look for aborted connections and attempt to restart them. */
    for (i = 0; i < d->conf->num_connections; i++) {
        /* skip connection if setup thread hasn't released the lock yet */
        if (pthread_mutex_trylock(&d->conn[i].lock))
            continue;

        if (!d->conn[i].enabled && d->conn[i].currentbyte < d->conn[i].lastbyte) {
            if (!d->conn[i].state) {
                // Wait for termination of this thread
                pthread_join(*(d->conn[i].setupThread), NULL);

                conn_set(&d->conn[i], url_ptr->text);
                url_ptr = url_ptr->next;
                /* app->conn[i].local_if = app->conf->interfaces->text;
                   app->conf->interfaces = app->conf->interfaces->next; */
                if (d->conf->verbose >= 2)
                    logd ("%s Connection %i downloading from %s:%i using interface %s",
                        uri, i, d->conn[i].host, d->conn[i].port, d->conn[i].localIf);

                d->conn[i].state = true;
                if (pthread_create (d->conn[i].setupThread, NULL, setup_thread, &d->conn[i]) == 0) {
                    d->conn[i].lastTransfer = gf_gettime();
                } else {
                    loge ("%s pthread error!!!", uri);
                    d->ready = -1;
                }
            } else {
                if (gf_gettime() > (d->conn[i].lastTransfer +
                        d->conf->reconnect_delay)) {
                    pthread_cancel(*d->conn[i].setupThread);
                    d->conn[i].state = false;
                    pthread_join(*d->conn[i].setupThread, NULL);
                }
            }
        }
        pthread_mutex_unlock(&d->conn[i].lock);
    }

    /* Calculate current average speed and finish_time */
    d->bytesPerSecond = (off_t)((double)(d->bytesDone - d->startByte) / (gf_gettime() - d->startTime));
    if (d->bytesPerSecond != 0) {
        d->finishTime = (int)(d->startTime + (double)(d->size - d->startByte) / d->bytesPerSecond);
    } else {
        d->finishTime = INT_MAX;
    }

    /* Check speed. If too high, delay for some time to slow things
       down a bit. I think a 5% deviation should be acceptable. */
    if (d->conf->max_speed > 0) {
        max_speed_ratio = 1000 * d->bytesPerSecond / d->conf->max_speed;
        if (max_speed_ratio > 1050) {
            d->delayTime.tv_nsec += 10000000;
            if (d->delayTime.tv_nsec >= 1000000000) {
                d->delayTime.tv_sec++;
                d->delayTime.tv_nsec -= 1000000000;
            }
        } else if (max_speed_ratio < 950) {
            if (d->delayTime.tv_nsec >= 10000000) {
                d->delayTime.tv_nsec -= 10000000;
            } else if (d->delayTime.tv_sec > 0) {
                d->delayTime.tv_sec--;
                d->delayTime.tv_nsec += 999000000;
            } else {
                d->delayTime.tv_sec = 0;
                d->delayTime.tv_nsec = 0;
            }
        }
        if (gf_sleep(d->delayTime) < 0) {
            loge ("%s Error while enforcing throttling: %s", uri, strerror(errno));
            d->ready = -1;
            return false;
        }
    }

    /* Ready? */
    if (d->bytesDone == d->size)
        d->ready = 1;

    return true;
}

void http_free(DownloadData *d)
{
//    http_disconnect ();

}

inline static bool is_https (GUri* uri)
{
    return (0 & g_strcmp0 ("https", g_uri_get_scheme (uri)));
}


static int http_status_get (Http* http)
{
    g_return_val_if_fail (http, 0);

//    char *p = http->headers->p;
//    /* Skip protocol and code */
//    while (*p++ != ' ');
//    size_t len = strcspn(p + 1, " ");
//    if (len) {
//        gf_strlcpy(msg, p, min(len + 1, size));
//        return http->status;
//    }


    return 0;
}


static bool http_get_header_info (DownloadData* d)
{
    g_return_val_if_fail (d && d->data, false);

    Http* http = d->data;

    // connect
    const char* host = g_uri_get_host (d->uri);

    bool isHttps = is_https (d->uri);
    if (!http_connect (http, isHttps ? PROTO_HTTPS : PROTO_HTTP, NULL, host,
            isHttps ? PROTO_HTTPS_PORT : PROTO_HTTP_PORT, NULL, NULL, -1)) {
        return false;
    }

    // http header
    int ret = abuf_setup (http->headers, 40960);
    if (0 != ret) {
        http_free (d);
        loge ("abuf_setup error: %s", strerror (ret));
        return false;
    }

    ret = abuf_setup (http->request, 4096);
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

    // get resource information
    http_get (http, path);

    if (!http_exec (d->data)) {
        return false;
    }

//    d->total = http_size (http);
//    d->curr = http_size_from_range (http);

    http_disconnect (http);

    return true;
}


inline static void http_error (DownloadData* d)
{
    g_return_if_fail (d && d->data);

    Http* http = (Http*) d->data;
    g_autofree char* uri = g_uri_to_string (d->uri);

    loge ("uri: '%s' error code: %d", uri, http->status);

    logd ("\n======== request ==========\n%s\n====== request end ========\n"
         "======== response ==========\n%s\n====== response end ========\n", http->request, http->headers);
}


static void* setup_thread (void *c)
{
    Conn* conn = c;
    int oldstate;

    /* Allow this thread to be killed at any time. */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldstate);

    pthread_mutex_lock(&conn->lock);
    if (conn_setup(conn)) {
        conn->lastTransfer = gf_gettime();
        if (conn_exec(conn)) {
            conn->lastTransfer = gf_gettime();
            conn->enabled = true;
            goto out;
        }
    }

    conn_disconnect(conn);

out:
    conn->state = false;
    pthread_mutex_unlock(&conn->lock);

    return NULL;
}
