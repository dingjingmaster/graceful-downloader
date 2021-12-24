#include <stdio.h>

#include "http-request.h"

int main (int argc, char* argv[])
{
    HttpRequest* req = http_request_new ("www.baidu.com", "/index.html");

    printf ("============= request header ================\n");
    printf ("%s\n", http_request_get_string (req));
    printf ("=============================================\n");

    http_request_destroy (req);

    return 0;
}
