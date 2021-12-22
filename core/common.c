#include "common.h"

#include "log.h"
#include "utils.h"

#define MIN_CHUNK_WORTH     (100 * 1024)    /* 100 KB */

static void save_state (DownloadData* app)
{
    /* No use for such a file if the server doesn't support
       resuming anyway.. */
    if (!app->conn[0].supported)
        return;

    int fd;
    fd = stfile_open(app->filename, O_CREAT | O_TRUNC | O_WRONLY, 0666);
    if (fd == -1) {
        return;		/* Not 100% fatal.. */
    }

    ssize_t nwrite;
    (void)nwrite; /* workaround unused variable warning */
    nwrite = write(fd, &app->conf->num_connections, sizeof(app->conf->num_connections));
    g_return_if_fail (nwrite == sizeof(app->conf->num_connections));

    nwrite = write(fd, &app->bytesDone, sizeof (app->bytesDone));
    g_return_if_fail (nwrite == sizeof(app->bytesDone));

    for (int i = 0; i < app->conf->num_connections; i++) {
        nwrite = write(fd, &app->conn[i].currentbyte, sizeof(app->conn[i].currentbyte));
        g_return_if_fail (nwrite == sizeof(app->conn[i].currentbyte));
        nwrite = write(fd, &app->conn[i].lastbyte, sizeof(app->conn[i].lastbyte));
        g_return_if_fail (nwrite == sizeof(app->conn[i].lastbyte));
    }
    close(fd);
}

static void download_divide (DownloadData* app)
{
    /* Optimize the number of connections in case the file is small */
    off_t maxconns = max (1u, app->size / MIN_CHUNK_WORTH);
    if (maxconns < app->conf->num_connections)
        app->conf->num_connections = maxconns;

    /* Calculate each segment's size */
    off_t seg_len = app->size / app->conf->num_connections;

    if (!seg_len) {
        logd ("Too few bytes remaining, forcing a single connection");
        app->conf->num_connections = 1;
        seg_len = app->size;

        Conn *newConn = realloc(app->conn, sizeof (*app->conn));
        if (newConn)
            app->conn = newConn;
    }

    for (int i = 0; i < app->conf->num_connections; i++) {
        app->conn[i].currentbyte = seg_len * i;
        app->conn[i].lastbyte    = seg_len * i + seg_len;
    }

    /* Last connection downloads remaining bytes */
    seg_len = !seg_len ? 1 : seg_len;
    size_t tail = app->size % seg_len;
    app->conn[app->conf->num_connections - 1].lastbyte += tail;

    for (int i = 0; i < app->conf->num_connections; i++) {
        logd ("Downloading %jd-%jd using conn. %i", app->conn[i].currentbyte, app->conn[i].lastbyte, i);
    }
}

static void reactivate_connection (DownloadData* app, int thread)
{
    /* TODO Make the minimum also depend on the connection speed */
    off_t max_remaining = MIN_CHUNK_WORTH - 1;
    int idx = -1;

    if (app->conn[thread].enabled || app->conn[thread].currentbyte < app->conn[thread].lastbyte) {
        return;
    }

    for (int j = 0; j < app->conf->num_connections; j++) {
        off_t remaining = app->conn[j].lastbyte - app->conn[j].currentbyte;
        if (remaining > max_remaining) {
            max_remaining = remaining;
            idx = j;
        }
    }

    if (idx == -1) {
        return;
    }

    logd ("Reactivate connection %d", thread);

    app->conn[thread].lastbyte = app->conn[idx].lastbyte;
    app->conn[idx].lastbyte = app->conn[idx].currentbyte + max_remaining / 2;
    app->conn[thread].currentbyte = app->conn[idx].lastbyte;
}
