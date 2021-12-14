#ifndef _SSL_H
#define _SSL_H

#include <stdbool.h>
#include <openssl/ssl.h>

#include "conf.h"

void ssl_init (Conf *conf);
SSL* ssl_connect (int fd, char *hostname);
void ssl_disconnect (SSL *ssl);
bool ssl_validate_hostname (const char *hostname, const X509 *server_cert);

#endif
