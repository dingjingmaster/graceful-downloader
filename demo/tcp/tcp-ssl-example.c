#include <stdio.h>

#include "tcp.h"

int main (int argc, char* argv[])
{
    Tcp* tcp = tcp_new ();
    if (!tcp) {
        printf ("tcp_new error\n");
        return -1;
    }

    if (!tcp_connect (tcp, "www.baidu.com", 443, true, NULL, -1)) {
        printf ("tcp_connect error: %s\n", tcp->error->message);
        return -1;
    }

    const char* request= "GET /index.html HTTP/1.0\r\n"
                         "Host: www.baidu.com\r\n"
                         "Accept: */*\r\n"
                         "Accept-Encoding: identity\r\n\r\n";

    if (!tcp_write (tcp, request, strlen (request))) {
        printf ("tcp_write error: %s\n", tcp->error->message);
        return -1;
    }

    char buf[10240] = {0};
    if (!tcp_read (tcp, buf, sizeof (buf))) {
        printf ("tcp_read error: %s\n", tcp->error->message);
        return -1;
    }


    printf ("=============  request  ===========\n");
    printf ("%s\n", request);
    printf ("===================================\n");

    printf ("=============  respose  ===========\n");
    printf ("%s\n", buf);
    printf ("===================================\n");

    tcp_destroy (&tcp);

    return 0;
}
