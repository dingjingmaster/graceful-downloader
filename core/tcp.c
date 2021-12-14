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

#include "log.h"
#include "config.h"
#include "utils.h"

#ifndef TCP_FASTOPEN_CONNECT
#ifdef __linux__
#define TCP_FASTOPEN_CONNECT 30
#else /* __linux__ */
#define TCP_FASTOPEN_CONNECT 0
#endif /* __linux__ */
#endif

/*
 * Check if the given hostname is ipv6 literal
 * Returns 1 if true and 0 if false
 */
int is_ipv6_addr (const char *hostname)
{
    char buf[16] = {0}; /* Max buff size needed for inet_pton() */

    return hostname && 1 == inet_pton(AF_INET6, hostname, buf);
}

inline static void tcp_error(char *hostname, int port, const char *reason)
{
    loge ("Unable to connect to server %s:%i: %s\n", hostname, port, reason);
}

/* Get a TCP connection */
int tcp_connect (Tcp* tcp, char *hostname, int port, int secure, char *local_if, unsigned io_timeout)
{
    struct sockaddr_in local_addr;
    char portstr[10] = {0};
    struct addrinfo ai_hints;
    struct addrinfo* gai_results, *gai_result;
    int ret;
    int sock_fd = -1;

    memset (&local_addr, 0, sizeof(local_addr));
    if  (local_if) {
        if (!*local_if || tcp->ai_family != AF_INET) {
            local_if = NULL;
        } else {
            local_addr.sin_family = AF_INET;
            local_addr.sin_port = 0;
            local_addr.sin_addr.s_addr = inet_addr (local_if);
        }
    }

    snprintf (portstr, sizeof (portstr), "%d", port);

    memset (&ai_hints, 0, sizeof (ai_hints));
    ai_hints.ai_family = tcp->ai_family;
    ai_hints.ai_socktype = SOCK_STREAM;
    ai_hints.ai_flags = AI_ADDRCONFIG;
    ai_hints.ai_protocol = 0;

    ret = getaddrinfo (hostname, portstr, &ai_hints, &gai_results);
    if (ret != 0) {
        tcp_error (hostname, port, gai_strerror(ret));
        return -1;
    }

    gai_result = gai_results;
    do {
        int tcp_fastopen = -1;

        if (sock_fd != -1) {
            close (sock_fd);
            sock_fd = -1;
        }

        sock_fd = socket (gai_result->ai_family, gai_result->ai_socktype, gai_result->ai_protocol);
        if (sock_fd == -1) {
            continue;
        }

        if (local_if && gai_result->ai_family == AF_INET) {
            bind (sock_fd, (struct sockaddr *) &local_addr, sizeof(local_addr));
        }

        if (TCP_FASTOPEN_CONNECT) {
            tcp_fastopen = setsockopt (sock_fd, IPPROTO_TCP, TCP_FASTOPEN_CONNECT, NULL, 0);
        } else if (io_timeout) {
            /* Set O_NONBLOCK so we can timeout */
            fcntl (sock_fd, F_SETFL, O_NONBLOCK);
        }
        ret = connect (sock_fd, gai_result->ai_addr, gai_result->ai_addrlen);

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
        FD_SET (sock_fd, &fdset);
        struct timeval tout = { .tv_sec  = io_timeout };
        ret = select (sock_fd + 1, NULL, &fdset, NULL, &tout);

        /* Success? */
        if (ret != -1) {
            break;
        }
    } while ((gai_result = gai_result->ai_next));

    freeaddrinfo(gai_results);

    if (sock_fd == -1) {
        tcp_error (hostname, port, strerror(errno));
        return -1;
    }

    fcntl(sock_fd, F_SETFL, 0);

#ifdef HAVE_SSL
    if (secure) {
        tcp->ssl = ssl_connect (sock_fd, hostname);
        if (tcp->ssl == NULL) {
            close(sock_fd);
            return -1;
        }
    }
#endif /* HAVE_SSL */
    tcp->fd = sock_fd;

    /* Set I/O timeout */
    struct timeval tout = { .tv_sec  = io_timeout };
    setsockopt (sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tout, sizeof(tout));
    setsockopt (sock_fd, SOL_SOCKET, SO_SNDTIMEO, &tout, sizeof(tout));

    return 1;
}

ssize_t tcp_read (Tcp *tcp, void *buffer, int size)
{
#ifdef HAVE_SSL
    if (tcp->ssl != NULL) {
        return SSL_read (tcp->ssl, buffer, size);
    }
#endif

    return read (tcp->fd, buffer, size);
}

ssize_t tcp_write (Tcp *tcp, void *buffer, int size)
{
#ifdef HAVE_SSL
    if (tcp->ssl != NULL) {
        return SSL_write (tcp->ssl, buffer, size);
    }
#endif

    return write (tcp->fd, buffer, size);
}

void tcp_close (Tcp* tcp)
{
    if (tcp->fd > 0) {
#ifdef HAVE_SSL
        if (tcp->ssl != NULL) {
            ssl_disconnect (tcp->ssl);
            tcp->ssl = NULL;
        }
#endif
        close (tcp->fd);
        tcp->fd = -1;
    }
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
