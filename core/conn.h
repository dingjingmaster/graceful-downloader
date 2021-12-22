#ifndef _CONN_H
#define _CONN_H

#include "protocol-interface.h"


#define PROTO_SECURE_MASK           (1<<0)	/* bit 0 - 0 = insecure, 1 = secure */
#define PROTO_PROTO_MASK            (1<<1)	/* bit 1 = 0 = ftp,      1 = http   */

#define PROTO_INSECURE              (0<<0)
#define PROTO_SECURE                (1<<0)
#define PROTO_PROTO_FTP             (0<<1)
#define PROTO_PROTO_HTTP            (1<<1)

#define PROTO_IS_FTP(proto)         (((proto) & PROTO_PROTO_MASK) == PROTO_PROTO_FTP)
#define PROTO_IS_SECURE(proto)      (((proto) & PROTO_SECURE_MASK) == PROTO_SECURE)

#define PROTO_FTP                   (PROTO_PROTO_FTP|PROTO_INSECURE)
#define	PROTO_FTP_PORT              21

#define PROTO_FTPS                  (PROTO_PROTO_FTP|PROTO_SECURE)
#define	PROTO_FTPS_PORT             990

#define PROTO_HTTP                  (PROTO_PROTO_HTTP|PROTO_INSECURE)
#define	PROTO_HTTP_PORT             80

#define PROTO_HTTPS                 (PROTO_PROTO_HTTP|PROTO_SECURE)
#define	PROTO_HTTPS_PORT            443

#define PROTO_DEFAULT               PROTO_HTTP
#define PROTO_DEFAULT_PORT          PROTO_HTTP_PORT


struct _Conn
{
    Conf*               conf;

    int                 proto;
    int                 port;
    int                 proxy;
    char                host[MAX_STRING];
    char                dir [MAX_STRING];
    char                file[MAX_STRING];
    char                user[MAX_STRING];
    char                pass[MAX_STRING];
    char                outputFilename[MAX_STRING];

    Ftp                 ftp[1];
    Http                http[1];
    off_t               size; /* File size, not 'connection size'.. */
    off_t               currentbyte;
    off_t               lastbyte;
    Tcp*                tcp;
    bool                enabled;
    bool                supported;
    int                 lastTransfer;
    char*               message;
    char*               localIf;

    bool                state;
    pthread_t           setupThread[1];
    pthread_mutex_t     lock;
};

static inline int is_proto_http (int proto)
{
    return (proto & PROTO_PROTO_MASK) == PROTO_PROTO_HTTP;
}

int conn_set (Conn* conn, const char* set_url);
char *conn_url(char* dst, size_t len, Conn* conn);
void conn_disconnect(Conn* conn);
int conn_init(Conn* conn);
int conn_setup(Conn* conn);
int conn_exec(Conn* conn);
int conn_info(Conn* conn);
int conn_info_status_get(char *msg, size_t size, Conn *conn);
const char* scheme_from_proto(int proto);

#endif
