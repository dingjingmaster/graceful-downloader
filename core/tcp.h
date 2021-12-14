#ifndef _TCP_H
#define _TCP_H

#include <unistd.h>
#include <netinet/in.h>

//#ifdef HAVE_SSL
#include <openssl/ssl.h>
//#endif

typedef struct _Tcp     Tcp;

struct _Tcp
{
    int fd;
    sa_family_t ai_family;
    //#ifdef HAVE_SSL
    SSL *ssl;
    //#endif
};

int is_ipv6_addr (const char *hostname);
int tcp_connect (Tcp* tcp, char* hostname, int port, int secure, char *local_if, unsigned io_timeout);
void tcp_close (Tcp* tcp);

ssize_t tcp_read (Tcp* tcp, void *buffer, int size);
ssize_t tcp_write (Tcp* tcp, void *buffer, int size);

int get_if_ip(char *dst, size_t len, const char *iface);

#endif
