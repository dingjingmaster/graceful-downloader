#include "ssl.h"

#include <stdbool.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "conf.h"
#include "config.h"
#include "compat-ssl.h"

static pthread_mutex_t ssl_lock;
static bool ssl_inited = false;
static Conf *conf = NULL;

void ssl_init(Conf *global_conf)
{
    pthread_mutex_init(&ssl_lock, NULL);
    conf = global_conf;
}

static void ssl_startup(void)
{
    pthread_mutex_lock(&ssl_lock);
    if (!ssl_inited) {
        SSL_library_init();
        SSL_load_error_strings();

        ssl_inited = true;
    }
    pthread_mutex_unlock(&ssl_lock);
}

SSL* ssl_connect(int fd, char *hostname)
{
    X509 *server_cert;
    SSL_CTX *ssl_ctx;
    SSL *ssl;

    ssl_startup();

    ssl_ctx = SSL_CTX_new(SSLv23_client_method());
    if (!conf->insecure) {
        SSL_CTX_set_default_verify_paths(ssl_ctx);
        SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);
    }
    SSL_CTX_set_mode(ssl_ctx, SSL_MODE_AUTO_RETRY);

    ssl = SSL_new(ssl_ctx);
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, hostname);

    int err = SSL_connect(ssl);
    if (err <= 0) {
        fprintf(stderr, "SSL error: %s",
            ERR_reason_error_string(ERR_get_error()));
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }

    if (conf->insecure) {
        return ssl;
    }

    err = SSL_get_verify_result(ssl);
    if (err != X509_V_OK) {
        fprintf(stderr, "SSL error: Certificate error");
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }

    server_cert =  SSL_get_peer_certificate(ssl);
    if (server_cert == NULL) {
        fprintf(stderr, "SSL error: Certificate not found\n");
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }

    if (!ssl_validate_hostname(hostname, server_cert)) {
        fprintf(stderr, "SSL error: Hostname verification failed\n");
        X509_free(server_cert);
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }

    X509_free(server_cert);

    return ssl;
}

void ssl_disconnect(SSL *ssl)
{
    SSL_shutdown(ssl);
    SSL_free(ssl);
}
