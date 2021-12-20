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
#include "utils.h"
#include "config.h"

#ifndef TCP_FASTOPEN_CONNECT
#ifdef __linux__
#define TCP_FASTOPEN_CONNECT 30
#else /* __linux__ */
#define TCP_FASTOPEN_CONNECT 0
#endif /* __linux__ */
#endif

inline static void tcp_error(char *hostname, int port, const char *reason);


void tcp_close (Tcp* tcp)
{
    if (tcp->fd > 0) {
        if (tcp->secure) {
            SSL_shutdown (tcp->ssl);
            SSL_free (tcp->ssl);

            close (tcp->fd);

            SSL_CTX_free (tcp->sslCtx);
        } else {
            close (tcp->fd);
        }

        tcp->fd = -1;
    }
}


/* Get a TCP connection */
int tcp_connect (Tcp* tcp, char *hostname, int port, bool secure, char *localIf, unsigned ioTimeout)
{
    struct sockaddr_in localAddr;
    char portstr[10] = {0};
    struct addrinfo aiHints;
    struct addrinfo* gairesults, *gairesult;
    int ret;
    int sockfd = -1;

    tcp->secure = secure;

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
        tcp_error (hostname, port, gai_strerror(ret));
        return -1;
    }

    gairesult = gairesults;
    do {
        int tcp_fastopen = -1;

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
            tcp_fastopen = setsockopt (sockfd, IPPROTO_TCP, TCP_FASTOPEN_CONNECT, NULL, 0);
        } else if (ioTimeout) {
            /* Set O_NONBLOCK so we can timeout */
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
        if (tcp_fastopen != -1) {
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
        tcp_error (hostname, port, strerror(errno));
        return -1;
    }

    fcntl(sockfd, F_SETFL, 0);

    if (tcp->secure) {
        SSL_library_init ();
        OpenSSL_add_all_algorithms();
        SSL_load_error_strings();
        tcp->sslCtx = SSL_CTX_new (SSLv23_client_method());
        if (NULL == tcp->sslCtx) {
            tcp_error (hostname, port, "SSL_CTX_new error");
            return -1;
        }

        tcp->ssl = SSL_new (tcp->sslCtx);

        SSL_set_fd (tcp->ssl, sockfd);

        if (-1 == SSL_connect (tcp->ssl)) {
            tcp_error (hostname, port, "SSL_connect error");
            return -1;
        }
    }
    tcp->fd = sockfd;

    /* Set I/O timeout */
    struct timeval tout = { .tv_sec  = ioTimeout };
    setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, &tout, sizeof(tout));
    setsockopt (sockfd, SOL_SOCKET, SO_SNDTIMEO, &tout, sizeof(tout));

    return 1;
}


ssize_t tcp_read (Tcp *tcp, void *buffer, int size)
{
    return (tcp->secure ? SSL_read (tcp->ssl, buffer, size) : read (tcp->fd, buffer, size));
}


ssize_t tcp_write (Tcp *tcp, void *buffer, int size)
{
    return (tcp->secure ? SSL_write (tcp->ssl, buffer, size) : write (tcp->fd, buffer, size));
}


/**
 * Check if the given hostname is ipv6 literal
 * Returns 1 if true and 0 if false
 */
int is_ipv6_addr (const char *hostname)
{
    char buf[16] = {0}; /* Max buff size needed for inet_pton() */

    return hostname && 1 == inet_pton(AF_INET6, hostname, buf);
}

inline static void tcp_error (char *hostname, int port, const char *reason)
{
    loge ("Unable to connect to server %s:%i: %s", hostname, port, reason);
}


int get_if_ip (char *dst, size_t len, const char *iface)
{
    struct ifreq ifr;
    int ret, fd = socket (PF_INET, SOCK_DGRAM, IPPROTO_IP);

    if (fd < 0) {
        return 0;
    }

    memset (&ifr, 0, sizeof (struct ifreq));

    gf_strlcpy (ifr.ifr_name, iface, sizeof(ifr.ifr_name));
    ifr.ifr_addr.sa_family = AF_INET;

    ret = !ioctl (fd, SIOCGIFADDR, &ifr);
    if (ret) {
        struct sockaddr_in *x = (struct sockaddr_in *) &ifr.ifr_addr;
        gf_strlcpy (dst, inet_ntoa (x->sin_addr), len);
    }

    close (fd);

    return ret;
}
