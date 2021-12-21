#include "download-manager.h"

#include <stdlib.h>

#include "log.h"
#include "thread-pool.h"

#include "http.h"


static GHashTable* gSchemaAndPortHash = NULL;
static GHashTable* gSchemaAndDownloader = NULL;
static GHashTable* gHostAndUserInfo = NULL;

void* download_worker (Downloader* d);


bool protocol_register ()
{
    if (!gSchemaAndPortHash) {
        gSchemaAndPortHash = g_hash_table_new (g_str_hash, g_str_equal);
    }

    g_return_val_if_fail (gSchemaAndPortHash, false);

    if (!gHostAndUserInfo) {
        gHostAndUserInfo = g_hash_table_new (g_str_hash, g_str_equal);
    }
    g_return_val_if_fail (gHostAndUserInfo, false);

    if (!gSchemaAndDownloader) {
        gSchemaAndDownloader = g_hash_table_new (g_str_hash, (void*) g_direct_hash);
    }
    g_return_val_if_fail (gSchemaAndDownloader, false);

    // schema and port
    g_hash_table_insert (gSchemaAndPortHash, "http", "80");
    g_hash_table_insert (gSchemaAndPortHash, "https", "443");
//    g_hash_table_insert (gSchemaAndPortHash, "ftp", "21");

    // schema and downloader


    return true;
}

void protocol_unregister ()
{
    if (gSchemaAndPortHash) g_hash_table_unref (gSchemaAndPortHash);
    if (gHostAndUserInfo)   g_hash_table_unref (gHostAndUserInfo);
}

GUri* url_Analysis (const char* url)
{
    g_return_val_if_fail (url, NULL);
    logd ("get uri: %s", url);

    g_return_val_if_fail (gSchemaAndPortHash, NULL);

    char** arr = g_strsplit (url, "://", -1);
    int len = g_strv_length (arr);
    g_autoptr (GUri) uri = NULL;
    g_autoptr (GError) error = NULL;
    switch (len) {
    case 1: {
        g_autofree char* turi = g_strdup_printf ("http://%s", url);
        uri = g_uri_parse (turi, G_URI_FLAGS_NONE, &error);
        if (error) {
            logi ("input url '%s' => '%s' parse error: %s", url, turi, error->message);
            goto out;
        }

        logd ("input url '%s' use default http protocol!", url);
    }
    break;
    case 2: {
        char* schema = arr[0];
        // check schema is supported
        if (g_hash_table_contains (gSchemaAndPortHash, schema)) {
            logd ("'%s' find schema '%s'", url, schema);
            uri = g_uri_parse (url, G_URI_FLAGS_NONE, &error);
            if (error) {
                logi ("input url '%s' parse error: %s", url, error->message);
                goto out;
            }
            logd ("input url '%s' use protocol '%s'!", url, schema);
        }
    }
    break;
    default: {
        logd ("Not found schema for uri '%s'", url);
    }
    }

out:
    if (arr) g_strfreev (arr);

    return uri ? g_uri_ref (uri) : NULL;
}

GList* get_supported_schema ()
{
    GList* l = g_hash_table_get_keys (gSchemaAndPortHash);

    GList* r = g_list_copy_deep (l, (void*) g_strdup, NULL);

    g_list_free (l);

    return r;
}

void download (const DownloadTask *data)
{
    g_return_if_fail (data);

    for (GList* l = data->uris; NULL != l; l = l->next) {
        g_autofree gchar* name = NULL;

        if (NULL == l->data)    continue;

        const char* path = g_uri_get_path ((GUri*) (l->data));
        if (path) {
            // name
            g_autofree char* path1 = g_uri_unescape_string (path, NULL);
            if (path1) {
                char** arr = g_strsplit (path1, "/", -1);
                int len = g_strv_length (arr);
                if (len > 0) {
                    name = g_strdup (arr[len - 1]);
                }
                g_strfreev (arr);
            }
        }

        //
        DownloadData* data1 = g_malloc0 (sizeof (DownloadData));
        if (!data1) {
            logd ("DownloadData g_malloc0 error");
            continue;
        }

        Downloader* dd = g_malloc0 (sizeof (Downloader));
        if (!dd) {
            logd ("Downloader g_malloc0 error");
            if (data1) g_free (data1);
            continue;
        }

        if (data->dir)      data1->outputDir = g_strdup (data->dir);
        if (l->data)        data1->uri = g_uri_ref (l->data);
        if (name)           data1->outputName = g_strdup (name);

        // use default directory
        if (!data1->outputDir) {
            data1->outputDir = g_strdup (g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD));
        }

        // use default name
        if (!data1->outputName) {
            g_autofree char* turi = g_uri_to_string (l->data);
            g_autofree char* ppath = g_base64_encode ((void*) turi, strlen(turi));
            data1->outputName = g_strdup (ppath);
        }

        dd->data = data1;

        thread_pool_add_work ((void*) download_worker, dd);
    }
}

void* download_worker (Downloader* d)
{
    g_return_val_if_fail (d && d->data /*&& d->method*/, NULL);

    g_autofree char* uri = g_uri_to_string (d->data->uri);

    logd ("start download, uri: %s, save to: %s/%s", uri, d->data->outputDir, d->data->outputName);

    ////////////////////////////////////////////
    // test
    http_init (d->data);

    http_download (d->data);

    return NULL;
}
