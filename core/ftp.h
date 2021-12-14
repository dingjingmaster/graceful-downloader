#ifndef _FTP_H
#define _FTP_H

#include "tcp.h"
#include "global.h"

#define FTP_PASSIVE	1
#define FTP_PORT	2

struct _Ftp
{
    char cwd[MAX_STRING];
    char *message;
    int status;
    Tcp tcp;
    Tcp data_tcp;
    int proto;
    int ftp_mode;
    char *local_if;
};

int ftp_connect (Ftp* conn, int proto, char *host, int port, char *user, char *pass, unsigned io_timeout);
void ftp_disconnect (Ftp* conn);
int ftp_wait (Ftp* conn);

#ifdef __GNUC__
__attribute__((format(printf, 2, 3)))
#endif /* __GNUC__ */

int ftp_command (Ftp* conn, const char *format, ...);
int ftp_cwd (Ftp* conn, char *cwd);
int ftp_data (Ftp* conn, unsigned io_timeout);
off_t ftp_size (Ftp* conn, char *file, int maxredir, unsigned io_timeout);

#endif
