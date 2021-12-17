#include "download-manager.h"

#include <stdlib.h>

#include "log.h"
#include "thread-pool.h"


static GHashTable* gIpAndPortHash = NULL;
static GHashTable* gHostAndUserInfo = NULL;


bool protocol_register ()
{
    if (!gIpAndPortHash) {
        gIpAndPortHash = g_hash_table_new (g_str_hash, g_str_equal);
    }

    g_return_val_if_fail (gIpAndPortHash, false);

    if (!gHostAndUserInfo) {
        gHostAndUserInfo = g_hash_table_new (g_str_hash, g_str_equal);
    }

    g_hash_table_insert (gIpAndPortHash, "http", "80");
//    g_hash_table_insert (gIpAndPortHash, "https", "443");
//    g_hash_table_insert (gIpAndPortHash, "ftp", "21");


    return true;
}

GUri* url_Analysis (const char* url)
{
    g_return_val_if_fail (url, NULL);
    logd ("get uri: %s", url);

    g_return_val_if_fail (gIpAndPortHash, NULL);

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
        if (g_hash_table_contains (gIpAndPortHash, schema)) {
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

void protocol_unregister ()
{
    if (gIpAndPortHash)     g_hash_table_unref (gIpAndPortHash);
    if (gHostAndUserInfo)   g_hash_table_unref (gHostAndUserInfo);
}

GList* get_supported_schema ()
{
    GList* l = g_hash_table_get_keys (gIpAndPortHash);

    GList* r = g_list_copy_deep (l, (void*) g_strdup, NULL);

    g_list_free (l);

    return r;
}

void download (const DownloadTask *data)
{
    g_return_if_fail (data);

    for (GList* l = data->uris; NULL != l; l = l->next) {
        g_autofree gchar* name = NULL;

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

        dd->data = data1;

        // FIXME:// method
        thread_pool_add_work (NULL, dd);
    }
}
