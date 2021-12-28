#include "download-manager.h"

#include <stdlib.h>

#include "log.h"
#include "dm-http.h"
#include "thread-pool.h"


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
    DownloadMethod* m = g_malloc0 (sizeof (DownloadMethod));
    g_return_val_if_fail (m, false);
    m->init = dm_http_init;
    m->download = dm_http_download;
    m->free = dm_http_free;

    g_hash_table_insert (gSchemaAndDownloader, "http", m);
    g_hash_table_insert (gSchemaAndDownloader, "https", m);


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

        g_autofree char* turi = g_uri_to_string (l->data);
        const char* schema = g_uri_get_scheme ((GUri*) l->data);
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
        Downloader* dd = g_malloc0 (sizeof (Downloader));
        if (!dd) {
            logd ("Downloader g_malloc0 error");
            goto error;
        }

        DownloadData* data1 = g_malloc0 (sizeof (DownloadData));
        if (!data1) {
            logd ("DownloadData g_malloc0 error");
            goto error;
        }
        dd->data = data1;

        if (l->data)        data1->uri = g_uri_ref (l->data);

        if (!name || g_str_has_suffix (turi, "/")) {
            name = g_base64_encode ((void*) turi, strlen(turi));
        }

        if (data->dir) {
            data1->outputName = g_strdup_printf ("%s/%s", data->dir, name);
        } else {
            const char* dir = g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD);
            data1->outputName = g_strdup_printf ("%s/%s", dir, name);
        }

        // download methos
        if (!g_hash_table_contains (gSchemaAndDownloader, schema)) {
            logd ("not found '%s' downloader", turi);
            goto error;
        }

        dd->method = (DownloadMethod*) g_hash_table_lookup (gSchemaAndDownloader, schema);

        thread_pool_add_work ((void*) download_worker, dd);

        continue;

    error:
        if (dd && dd->data)     g_free (dd->data);
        if (dd)                 g_free (dd);

        continue;
    }
}

void* download_worker (Downloader* d)
{
    g_return_val_if_fail (d && d->data && d->method, NULL);

    g_autofree char* uri = g_uri_to_string (d->data->uri);

    logd ("start download, uri: %s, save to: %s", uri, d->data->outputName);

    if (!d->method->init || !d->method->init (d->data)) {
        loge ("uri: %s, downloader init error!", uri);
        return NULL;
    }

    if (!d->method->download || !d->method->download (d->data)) {
        loge ("uri: %s, downloader download error!", uri);
        return NULL;
    }

    if (d->method->free) {
        d->method->free(d->data);
    }

    return NULL;
}


