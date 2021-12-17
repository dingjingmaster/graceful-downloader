#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H
#include <stdbool.h>
#include <gio/gio.h>

#include "protocol-interface.h"

typedef struct _DownloadTask    DownloadTask;

struct _DownloadTask
{
    GList*          uris;
    char*           dir;
};


/**
 * @brief 此函数里用来注册各个协议的下载器
 * @return 出错则返回 false, 正常则返回 true
 */
bool protocol_register ();


/**
 * @brief 取消注册, 主要是注册需要的一些内存释放
 */
void protocol_unregister ();


/**
 * @brief 返回当前支持的一些下载协议
 * @return 返回支持下载协议的字符串列表，需要深度释放
 */
GList* get_supported_schema ();


/**
 * @brief 分析是否支持这种 url 协议下载
 * @param url 表示要下载的 url
 * @return 返回值需要使用 g_uri_unref 释放
 */
GUri* url_Analysis (const char* url);


/**
 * @brief 开始排队执行下载任务
 * @param data 要下载的数据信息
 */
void download (const DownloadTask* data);

#endif // DOWNLOADMANAGER_H
