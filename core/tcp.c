#include "tcp.h"

#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/in_systm.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "log.h"

enum _TcpError {
    TCP_ERROR_TYPE_SSL = 1,
    TCP_ERROR_TYPE_HOST,
    TCP_ERROR_TYPE_ERROR,
    TCP_ERROR_TYPE_MEM_INSUFFICIENT,            /* insufficient memory */
};
typedef enum _TcpError          TcpError;

static const char* gCertFile = "/etc/ssl/certs/ca-certificates.crt";

static inline void tcp_error (GError**, TcpError err, const char* errStr);

static int tcp_buf_free_size (Tcp* tcp);
static int tcp_get_host_by_name (const char* host, struct sockaddr_in* sinp);


Tcp *tcp_new ()
{
    Tcp* tcp = g_malloc0 (sizeof (Tcp));
    if (!tcp) {
        logd ("g_malloc0 error");
        goto error;
    }

    tcp->sock = -1;
    tcp->useSSL = false;
    tcp->nTimeoutInSecond = -1;

    return tcp;

error:
    if (tcp)                g_free (tcp);

    return NULL;
}

void tcp_destroy(Tcp** tcp)
{
    g_return_if_fail (tcp && *tcp);

    tcp_close (*tcp);

    if ((*tcp)->error) g_error_free ((*tcp)->error);

    free (*tcp);

    *tcp = NULL;
}

void tcp_close(Tcp *tcp)
{
    g_return_if_fail (tcp);

    if (tcp->useSSL) {
        if (tcp->ssl) {
            SSL_get_shutdown (tcp->ssl);
            if (-1 != tcp->sock) {
                close (tcp->sock);
                tcp->sock = -1;
            }
            SSL_free (tcp->ssl);
            tcp->ssl = NULL;
        }

        if (tcp->sslCtx) {
            SSL_CTX_free (tcp->sslCtx);
        }

        if (tcp->sslCert) {
            X509_free (tcp->sslCert);
            tcp->sslCert = NULL;
        }
        tcp->useSSL = false;
    }

    if (-1 != tcp->sock) {
        close (tcp->sock);
        tcp->sock = -1;
    }
}

bool tcp_connect(Tcp *tcp, const char *hostname, int port, bool secure, const char *localIf, unsigned ioTimeout)
{
    g_return_val_if_fail (tcp, false);

    struct sockaddr_in localAddr;
    char portstr[10] = {0};
    struct addrinfo aiHints;
    struct addrinfo* gairesults, *gairesult;
    int ret;
    int sockfd = -1;

    tcp->useSSL = secure;

    memset (&localAddr, 0, sizeof(localAddr));
    if  (localIf) {
        if (!*localIf || tcp->aiFamily != AF_INET) {
            localIf = NULL;
        } else {
            localAddr.sin_family = AF_INET;
            localAddr.sin_port = 0;
            localAddr.sin_addr.s_addr = inet_addr (localIf);
        }
    }

    snprintf (portstr, sizeof (portstr), "%d", port);

    memset (&aiHints, 0, sizeof (aiHints));
    aiHints.ai_family = tcp->aiFamily;
    aiHints.ai_socktype = SOCK_STREAM;
    aiHints.ai_flags = AI_ADDRCONFIG;
    aiHints.ai_protocol = 0;

    ret = getaddrinfo (hostname, portstr, &aiHints, &gairesults);
    if (ret != 0) {
        tcp_error (&tcp->error, TCP_ERROR_TYPE_HOST, gai_strerror(ret));
        return false;
    }

    gairesult = gairesults;
    do {
        int tcpFastopen = -1;

        if (sockfd != -1) {
            close (sockfd);
        }

        sockfd = socket (gairesult->ai_family, gairesult->ai_socktype, gairesult->ai_protocol);
        if (sockfd == -1) {
            continue;
        }

        if (localIf && gairesult->ai_family == AF_INET) {
            bind (sockfd, (struct sockaddr *) &localAddr, sizeof(localAddr));
        }

        if (TCP_FASTOPEN_CONNECT) {
            tcpFastopen = setsockopt (sockfd, IPPROTO_TCP, TCP_FASTOPEN_CONNECT, NULL, 0);
        } else if (ioTimeout) {
            fcntl (sockfd, F_SETFL, O_NONBLOCK);
        }
        ret = connect (sockfd, gairesult->ai_addr, gairesult->ai_addrlen);

        /* Already connected maybe? */
        if (ret != -1) {
            break;
        }

        if (errno != EINPROGRESS) {
            continue;
        }

        /* With TFO we must assume success */
        if (tcpFastopen != -1) {
            break;
        }

        /* Wait for the connection */
        fd_set fdset;
        FD_ZERO (&fdset);
        FD_SET (sockfd, &fdset);
        struct timeval tout = { .tv_sec  = ioTimeout };
        ret = select (sockfd + 1, NULL, &fdset, NULL, &tout);

        /* Success? */
        if (ret != -1) {
            break;
        }
    } while ((gairesult = gairesult->ai_next));

    freeaddrinfo(gairesults);

    if (sockfd == -1) {
        tcp_error (&tcp->error, TCP_ERROR_TYPE_ERROR, strerror(errno));
        return false;
    }

    fcntl(sockfd, F_SETFL, 0);

    if (tcp->useSSL) {
        if (!tcp->sslInitialized) {
            SSLeay_add_ssl_algorithms ();
            tcp->sslMethod = SSLv23_client_method ();
            SSL_load_error_strings ();
            tcp->sslCtx = SSL_CTX_new (tcp->sslMethod);
            if (NULL == tcp->sslCtx) {
                tcp_error (&tcp->error, TCP_ERROR_TYPE_SSL, NULL);
                return false;
            } else {
                if (0 != SSL_CTX_load_verify_locations (tcp->sslCtx, gCertFile, NULL)) {
                    tcp->sslInitialized = true;
                } else {
                    tcp_error (&tcp->error, TCP_ERROR_TYPE_SSL, NULL);
                    return false;
                }
            }
            tcp->ssl = SSL_new (tcp->sslCtx);
        }

        SSL_set_fd (tcp->ssl, sockfd);

        if (-1 == SSL_connect (tcp->ssl)) {
            tcp_error (&tcp->error, TCP_ERROR_TYPE_SSL, NULL);
            return false;
        }
    }
    tcp->sock = sockfd;

    /* Set I/O timeout */
    struct timeval tout = { .tv_sec  = ioTimeout };
    setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, &tout, sizeof(tout));
    setsockopt (sockfd, SOL_SOCKET, SO_SNDTIMEO, &tout, sizeof(tout));

    return true;
}


static inline void tcp_error (GError** error, TcpError err, const char* errStr)
{
    g_return_if_fail (error);

    if (*error)    g_error_free (*error);

    if (TCP_ERROR_TYPE_SSL == err) {
        char buf[1024] = {0};
        ERR_error_string_n (ERR_peek_error (), buf, sizeof (buf) - 1);
        if (strlen (buf) > 0) {
            *error = g_error_new_literal (1, err, g_strdup (buf));
        }
    } else if (errStr) {
        *error = g_error_new_literal (1, err, g_strdup (errStr));
    } else {
        *error = g_error_new_literal (1, TCP_ERROR_TYPE_ERROR, g_strdup ("Unknow error"));
    }
}


ssize_t tcp_read(Tcp *tcp, void *buffer, int size)
{
    return (tcp->useSSL ? SSL_read (tcp->ssl, buffer, size) : read (tcp->sock, buffer, size));
}

ssize_t tcp_write(Tcp *tcp, const void *buffer, int size)
{
    return (tcp->useSSL ? SSL_write (tcp->ssl, buffer, size) : write (tcp->sock, buffer, size));
}
