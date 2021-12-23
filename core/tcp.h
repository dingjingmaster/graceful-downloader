#ifndef TCP_H
#define TCP_H

#include <unistd.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <openssl/ssl.h>

#include <gio/gio.h>

typedef struct _Tcp             Tcp;

struct _Tcp {
    char               *host;
    char               *proxyHost;
    short               port;

    int                 sock;

    sa_family_t         aiFamily;
    int                 nTimeoutInSecond;
    GError             *error;                 /* a hint as to an error */

    /* SSL support. we always have a USE_SSL var, even if compiled
     * without SSL; it's just always FALSE unless SSL is compiled in. */
    bool                 useSSL;
    bool                 sslInitialized;
    SSL                 *ssl;
    const SSL_METHOD    *sslMethod;
    X509                *sslCert;
    SSL_CTX             *sslCtx;
};


Tcp* tcp_new ();

/**
 * @brief 关闭 TCP 连接
 * @param tcp 结构体
 */
void tcp_close (Tcp* tcp);

/**
 * @brief 创建 TCP 连接
 * @param tcp Tcp结构体
 * @param hostname 需要连接的域名
 * @param port 要连接的端口
 * @param secure 是否使用 https:// 协议
 * @param localIf
 * @param ioTimout
 *
 * @return 成功返回 true， 失败返回 false
 */
bool tcp_connect (Tcp* tcp, const char *hostname, int port, bool useSSL, const char *localIf, unsigned ioTimeout);

/**
 * @brief 从 socket 读取数据
 * @param tcp
 * @param buffer 读取的 buffer
 * @param size 缓存区大小
 *
 * @return 成功,返回读取到的数据大小
 */
ssize_t tcp_read (Tcp* tcp, void *buffer, int size);

/**
 * @brief 往 socket 写数据
 * @param tcp
 * @param buffer 写入数据的 buffer
 * @param size 写入数据长度
 *
 * @return 成功,返回写入数据的大小
 */
ssize_t tcp_write (Tcp* tcp, const void *buffer, int size);


/**
 * @brief 销毁 Tcp 结构
 */
void tcp_destroy        (Tcp* tcp);


#endif // TCP_H
