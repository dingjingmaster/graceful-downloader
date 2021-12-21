#ifndef _TCP_H
#define _TCP_H

#include <unistd.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <openssl/ssl.h>

typedef struct _Tcp     Tcp;

struct _Tcp
{
    bool            secure;         // is use ssl

    int             fd;
    sa_family_t     aiFamily;

    SSL             *ssl;
    SSL_CTX         *sslCtx;
};


/**
 * @brief 关闭 TCP 连接
 * @param tcp 需要销毁的 tcp 结构体
 */
void    tcp_close       (Tcp* tcp);

/**
 * @brief 创建 TCP 连接
 * @param tcp Tcp结构体
 * @param hostname 需要连接的域名
 * @param port 要连接的端口
 * @param secure 是否使用 https:// 协议
 * @param localIf
 * @param ioTimout
 *
 * @return 成功返回 0， 失败返回 -1
 */
int     tcp_connect     (Tcp* tcp, const char* hostname, int port, bool secure, const char *localIf, unsigned ioTimeout);

/**
 * @brief 从 socket 读取数据
 * @param tcp
 * @param buffer 读取的 buffer
 * @param size 缓存区大小
 *
 * @return 成功,返回读取到的数据大小
 */
ssize_t tcp_read        (Tcp* tcp, void *buffer, int size);

/**
 * @brief 往 socket 写数据
 * @param tcp
 * @param buffer 写入数据的 buffer
 * @param size 写入数据长度
 *
 * @return 成功,返回写入数据的大小
 */
ssize_t tcp_write       (Tcp* tcp, void *buffer, int size);

int     is_ipv6_addr    (const char *hostname);
int     get_if_ip       (char *dst, size_t len, const char *iface);

#endif
