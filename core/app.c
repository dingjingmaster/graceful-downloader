#include "app.h"

#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <gio/gio.h>
#include <sys/time.h>

#include "log.h"
#include "utils.h"


#define MIN_CHUNK_WORTH     (100 * 1024)    /* 100 KB */


static void app_divide      (App* app);
static void app_message     (App* app, const char *format, ...);

static int stfile_unlink    (const char *bname);
static int stfile_access    (const char *bname, int mode);
static int stfile_open      (const char *bname, int flags, mode_t mode);

static void save_state      (App* app);
static void *setup_thread   (void *);
static char*stfile_makename (const char* bname);
static void reactivate_connection (App* app, int thread);

static char *buffer = NULL;

App *app_new (Conf *conf, int count, const Search *res)
{
    int status;
    uint64_t delay;
    Url *u = NULL;
    char *s;
    int i;

    g_return_val_if_fail (count && res, NULL);

    App *app = calloc (1, sizeof(App));
    if (!app) {
        logd ("calloc error!");
        goto nomem;
    }

    app->conf = conf;
    app->conn = calloc (app->conf->num_connections, sizeof(Conn));
    if (!app->conn) {
        logd ("calloc error!");
        goto nomem;
    }

    for (i = 0; i < app->conf->num_connections; ++i) {
        pthread_mutex_init(&app->conn[i].lock, NULL);
    }

    if (app->conf->max_speed > 0) {
        /* max_speed / buffer_size < .5 */
        if (16 * app->conf->max_speed / app->conf->buffer_size < 8) {
            if (app->conf->verbose >= 2)
                app_message (app, "Buffer resized for this speed.");
            app->conf->buffer_size = app->conf->max_speed;
        }

        delay = 1000000000 * app->conf->buffer_size * app->conf->num_connections / app->conf->max_speed;

        app->delay_time.tv_sec  = delay / 1000000000;
        app->delay_time.tv_nsec = delay % 1000000000;
    }

    if (buffer == NULL) {
        buffer = malloc (app->conf->buffer_size);
        if (!buffer)
            goto nomem;
    }

    u = malloc (sizeof(Url) * count);
    if (!u) {
        goto nomem;
    }

    app->url = u;

    for (i = 0; i < count; i++) {
        gf_strlcpy(u[i].text, res[i].url, sizeof(u[i].text));
        u[i].next = &u[i + 1];
    }
    u[count - 1].next = u;

    app->conn[0].conf = app->conf;
    if (!conn_set(&app->conn[0], app->url->text)) {
        app_message(app, "Could not parse URL.\n");
        app->ready = -1;
        return app;
    }

    app->conn[0].local_if = app->conf->interfaces->text;
    app->conf->interfaces = app->conf->interfaces->next;

    gf_strlcpy(app->filename, app->conn[0].file, sizeof(app->filename));
    http_decode(app->filename);

    if ((s = strchr(app->filename, '?')) != NULL && app->conf->strip_cgi_parameters) {
        *s = 0;		/* Get rid of CGI parameters */
    }

    if (*app->filename == 0) {
        gf_strlcpy(app->filename, app->conf->default_filename, sizeof(app->filename));
    }

    if (app->conf->no_clobber && access (app->filename, F_OK) == 0) {
        int ret = stfile_access (app->filename, F_OK);
        if (ret) {
            logi ("File '%s' already there; not retrieving.", app->filename);
            app->ready = -1;
            return app;
        }

        logi ("Incomplete download found, ignoring no-clobber option\n");
    }

    do {
        if (!conn_init(&app->conn[0])) {
            app_message(app, "%s", app->conn[0].message);
            app->ready = -1;
            return app;
        }

        /* This does more than just checking the file size, it all
         * depends on the protocol used. */
        status = conn_info(&app->conn[0]);
        if (!status) {
            char msg[80];
            int code = conn_info_status_get(msg, sizeof(msg), app->conn);
            loge ("ERROR %d: %s.", code, msg);
            app->ready = -1;
            return app;
        }
    } while (status == -1);

    conn_url(app->url->text, sizeof(app->url->text) - 1, app->conn);
    app->size = app->conn[0].size;
    if (app->conf->verbose > 0) {
        if (app->size != LLONG_MAX) {
            char hsize[32];
            app_size_human(hsize, sizeof(hsize), app->size);
            app_message (app, "File size: %s (%jd bytes)", hsize, app->size);
        } else {
            app_message (app, "File size: unavailable");
        }
    }

    /* Wildcards in URL --> Get complete filename */
    if (app->filename[strcspn(app->filename, "*?")])
        gf_strlcpy(app->filename, app->conn[0].file,
            sizeof(app->filename));

    if (*app->conn[0].output_filename != 0) {
        gf_strlcpy(app->filename, app->conn[0].output_filename, sizeof(app->filename));
    }

    return app;

nomem:
    app_close (app);
    printf("%s\n", strerror(errno));

    return NULL;
}

int app_open(App *app)
{
    int i, fd;
    ssize_t nread;

    if (app->conf->verbose > 0)
        app_message(app, "Opening output file %s", app->filename);

    app->outfd = -1;

    /* Check whether server knows about RESTart and switch back to
       single connection download if necessary */
    if (!app->conn[0].supported) {
        app_message(app, "Server unsupported, starting from scratch with one connection.");
        app->conf->num_connections = 1;
        void *new_conn = realloc(app->conn, sizeof(Conn));
        if (!new_conn) {
            return 0;
        }

        app->conn = new_conn;
        app_divide (app);
    } else if ((fd = stfile_open(app->filename, O_RDONLY, 0)) != -1) {
        int old_format = 0;
        off_t stsize = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);

        nread = read(fd, &app->conf->num_connections,
            sizeof(app->conf->num_connections));
        if (nread != sizeof(app->conf->num_connections)) {
            loge ("%s.st: Error, truncated state file", app->filename);
            close(fd);
            return 0;
        }

        if (app->conf->num_connections < 1) {
            loge ("Bogus number of connections stored in state file");
            close(fd);
            return 0;
        }

        if (stsize < (off_t)(sizeof(app->conf->num_connections) +
                sizeof(app->bytes_done) +
                2 * app->conf->num_connections *
                    sizeof(app->conn[0].currentbyte))) {
            /* FIXME this might be wrong, the file may have been
             * truncated, we need another way to check. */
            logd ("State file has old format.");
            old_format = 1;
        }

        void *new_conn = realloc(app->conn, sizeof(Conn) * app->conf->num_connections);
        if (!new_conn) {
            close(fd);
            return 0;
        }
        app->conn = new_conn;

        memset(app->conn + 1, 0, sizeof(Conn) * (app->conf->num_connections - 1));

        if (old_format) {
            app_divide(app);
        }

        nread = read(fd, &app->bytes_done, sizeof(app->bytes_done));
        assert(nread == sizeof(app->bytes_done));
        for (i = 0; i < app->conf->num_connections; i++) {
            nread = read(fd, &app->conn[i].currentbyte, sizeof(app->conn[i].currentbyte));
            assert(nread == sizeof(app->conn[i].currentbyte));
            if (!old_format) {
                nread = read(fd, &app->conn[i].lastbyte, sizeof(app->conn[i].lastbyte));
                assert(nread == sizeof(app->conn[i].lastbyte));
            }
        }

        app_message(app, "State file found: %jd bytes downloaded, %jd to go.", app->bytes_done, app->size - app->bytes_done);

        close(fd);

        if ((app->outfd = open(app->filename, O_WRONLY, 0666)) == -1) {
            app_message(app, "Error opening local file");
            return 0;
        }
    }

    /* If outfd == -1 we have to start from scrath now */
    if (app->outfd == -1) {
        app_divide(app);

        if ((app->outfd = open(app->filename, O_CREAT | O_WRONLY, 0666)) == -1) {
            app_message (app, "Error opening local file");
            return 0;
        }

        /* And check whether the filesystem can handle seeks to
           past-EOF areas.. Speeds things up. :) AFAIK this
           should just not happen: */
        if (lseek(app->outfd, app->size, SEEK_SET) == -1 &&
            app->conf->num_connections > 1) {
            /* But if the OS/fs does not allow to seek behind
               EOF, we have to fill the file with zeroes before
               starting. Slow.. */
            app_message(app, "Crappy filesystem/OS.. Working around. :-(");
            lseek(app->outfd, 0, SEEK_SET);
            memset(buffer, 0, app->conf->buffer_size);
            off_t j = app->size;
            while (j > 0) {
                ssize_t nwrite;

                if ((nwrite =
                        write(app->outfd, buffer,
                            min(j, app->conf->buffer_size))) < 0) {
                    if (errno == EINTR || errno == EAGAIN)
                        continue;
                    app_message(app, "Error creating local file");
                    return 0;
                }
                j -= nwrite;
            }
        }
    }

    return 1;
}

void app_start(App *app)
{
    int i;
    Url* url_ptr;

    /* HTTP might've redirected and FTP handles wildcards, so
       re-scan the URL for every conn */
    url_ptr = app->url;
    for (i = 0; i < app->conf->num_connections; i++) {
        conn_set(&app->conn[i], url_ptr->text);
        url_ptr = url_ptr->next;
        app->conn[i].local_if = app->conf->interfaces->text;
        app->conf->interfaces = app->conf->interfaces->next;
        app->conn[i].conf = app->conf;
        if (i)
            app->conn[i].supported = true;
    }

    if (app->conf->verbose > 0)
        app_message(app, "Starting download");

    for (i = 0; i < app->conf->num_connections; i++) {
        if (app->conn[i].currentbyte >= app->conn[i].lastbyte) {
            pthread_mutex_lock(&app->conn[i].lock);
            reactivate_connection(app, i);
            pthread_mutex_unlock(&app->conn[i].lock);
        } else if (app->conn[i].currentbyte < app->conn[i].lastbyte) {
            if (app->conf->verbose >= 2) {
                app_message(app, "Connection %i downloading from %s:%i using interface %s",
                    i, app->conn[i].host,
                    app->conn[i].port,
                    app->conn[i].local_if);
            }

            app->conn[i].state = true;
            if (pthread_create
                (app->conn[i].setup_thread, NULL, setup_thread,
                    &app->conn[i]) != 0) {
                app_message(app, "pthread error!!!");
                app->ready = -1;
            }
        }
    }

    /* The real downloading will start now, so let's start counting */
    app->start_time = app_gettime();
    app->ready = 0;
}

void app_do(App *app)
{
    fd_set fds[1];
    int hifd, i;
    off_t remaining, size;
    struct timeval timeval[1];
    Url *url_ptr;
    struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000};
    unsigned long long int max_speed_ratio;

    /* Create statefile if necessary */
    if (app_gettime() > app->next_state) {
        save_state(app);
        app->next_state = app_gettime() + app->conf->save_state_interval;
    }

    /* Wait for data on (one of) the connections */
    FD_ZERO(fds);
    hifd = 0;
    for (i = 0; i < app->conf->num_connections; i++) {
        /* skip connection if setup thread hasn't released the lock yet */
        if (!pthread_mutex_trylock(&app->conn[i].lock)) {
            if (app->conn[i].enabled) {
                FD_SET(app->conn[i].tcp->fd, fds);
                hifd = max(hifd, app->conn[i].tcp->fd);
            }
            pthread_mutex_unlock(&app->conn[i].lock);
        }
    }
    if (hifd == 0) {
        /* No connections yet. Wait... */
        if (gf_sleep(delay) < 0) {
            app_message(app,
                "Error while waiting for connection: %s",
                strerror(errno));
            app->ready = -1;
            return;
        }
        goto conn_check;
    }

    timeval->tv_sec = 0;
    timeval->tv_usec = 100000;
    if (select(hifd + 1, fds, NULL, NULL, timeval) == -1) {
        /* A select() error probably means it was interrupted
         * by a signal, or that something else's very wrong... */
        app->ready = -1;
        return;
    }

    /* Handle connections which need attention */
    for (i = 0; i < app->conf->num_connections; i++) {
        /* skip connection if setup thread hasn't released the lock yet */
        if (pthread_mutex_trylock(&app->conn[i].lock))
            continue;

        if (!app->conn[i].enabled)
            goto next_conn;

        if (!FD_ISSET(app->conn[i].tcp->fd, fds)) {
            time_t timeout = app->conn[i].last_transfer +
                app->conf->connection_timeout;
            if (app_gettime() > timeout) {
                if (app->conf->verbose)
                    app_message(app, "Connection %i timed out", i);
                conn_disconnect(&app->conn[i]);
            }
            goto next_conn;
        }

        app->conn[i].last_transfer = app_gettime();
        size =
            tcp_read(app->conn[i].tcp, buffer,
                app->conf->buffer_size);
        if (size == -1) {
            if (app->conf->verbose) {
                app_message(app, "Error on connection %i! Connection closed", i);
            }
            conn_disconnect(&app->conn[i]);
            goto next_conn;
        }

        if (size == 0) {
            if (app->conf->verbose) {
                /* Only abnormal behaviour if: */
                if (app->conn[i].currentbyte <
                        app->conn[i].lastbyte &&
                    app->size != LLONG_MAX) {
                    app_message(app, "Connection %i unexpectedly closed", i);
                } else {
                    app_message(app, "Connection %i finished", i);
                }
            }
            if (!app->conn[0].supported) {
                app->ready = 1;
            }
            conn_disconnect(&app->conn[i]);
            reactivate_connection(app, i);
            goto next_conn;
        }

        /* remaining == Bytes to go */
        remaining = app->conn[i].lastbyte - app->conn[i].currentbyte;
        if (remaining < size) {
            if (app->conf->verbose) {
                app_message(app, "Connection %i finished", i);
            }
            conn_disconnect(&app->conn[i]);
            size = remaining;
            /* Don't terminate, still stuff to write! */
        }
        /* This should always succeed.. */
        lseek(app->outfd, app->conn[i].currentbyte, SEEK_SET);
        if (write(app->outfd, buffer, size) != size) {
            app_message(app, "Write error!");
            app->ready = -1;
            pthread_mutex_unlock(&app->conn[i].lock);
            return;
        }
        app->conn[i].currentbyte += size;
        app->bytes_done += size;
        if (remaining == size)
            reactivate_connection(app, i);

    next_conn:
        pthread_mutex_unlock(&app->conn[i].lock);
    }

    if (app->ready)
        return;

conn_check:
    /* Look for aborted connections and attempt to restart them. */
    url_ptr = app->url;
    for (i = 0; i < app->conf->num_connections; i++) {
        /* skip connection if setup thread hasn't released the lock yet */
        if (pthread_mutex_trylock(&app->conn[i].lock))
            continue;

        if (!app->conn[i].enabled &&
            app->conn[i].currentbyte < app->conn[i].lastbyte) {
            if (!app->conn[i].state) {
                // Wait for termination of this thread
                pthread_join(*(app->conn[i].setup_thread),
                    NULL);

                conn_set(&app->conn[i], url_ptr->text);
                url_ptr = url_ptr->next;
                /* app->conn[i].local_if = app->conf->interfaces->text;
                   app->conf->interfaces = app->conf->interfaces->next; */
                if (app->conf->verbose >= 2)
                    app_message(app,
                        "Connection %i downloading from %s:%i using interface %s",
                        i, app->conn[i].host,
                        app->conn[i].port,
                        app->conn[i].local_if);

                app->conn[i].state = true;
                if (pthread_create
                    (app->conn[i].setup_thread, NULL,
                        setup_thread, &app->conn[i]) == 0) {
                    app->conn[i].last_transfer = app_gettime();
                } else {
                    app_message(app, "pthread error!!!");
                    app->ready = -1;
                }
            } else {
                if (app_gettime() > (app->conn[i].last_transfer +
                        app->conf->reconnect_delay)) {
                    pthread_cancel(*app->conn[i].setup_thread);
                    app->conn[i].state = false;
                    pthread_join(*app->conn[i].setup_thread, NULL);
                }
            }
        }
        pthread_mutex_unlock(&app->conn[i].lock);
    }

    /* Calculate current average speed and finish_time */
    app->bytes_per_second =
        (off_t)((double)(app->bytes_done - app->start_byte) /
            (app_gettime() - app->start_time));
    if (app->bytes_per_second != 0)
        app->finish_time =
            (int)(app->start_time +
                (double)(app->size - app->start_byte) /
                    app->bytes_per_second);
    else
        app->finish_time = INT_MAX;

    /* Check speed. If too high, delay for some time to slow things
       down a bit. I think a 5% deviation should be acceptable. */
    if (app->conf->max_speed > 0) {
        max_speed_ratio = 1000 * app->bytes_per_second /
            app->conf->max_speed;
        if (max_speed_ratio > 1050) {
            app->delay_time.tv_nsec += 10000000;
            if (app->delay_time.tv_nsec >= 1000000000) {
                app->delay_time.tv_sec++;
                app->delay_time.tv_nsec -= 1000000000;
            }
        } else if (max_speed_ratio < 950) {
            if (app->delay_time.tv_nsec >= 10000000) {
                app->delay_time.tv_nsec -= 10000000;
            } else if (app->delay_time.tv_sec > 0) {
                app->delay_time.tv_sec--;
                app->delay_time.tv_nsec += 999000000;
            } else {
                app->delay_time.tv_sec = 0;
                app->delay_time.tv_nsec = 0;
            }
        }
        if (gf_sleep(app->delay_time) < 0) {
            app_message(app, "Error while enforcing throttling: %s", strerror(errno));
            app->ready = -1;
            return;
        }
    }

    /* Ready? */
    if (app->bytes_done == app->size)
        app->ready = 1;
}

void app_close(App *app)
{
    if (!app)
        return;

    /* this function can't be called with a partly initialized app */
    assert(app->conn);

    /* Terminate threads and close connections */
    for (int i = 0; i < app->conf->num_connections; i++) {
        /* don't try to kill non existing thread */
        if (*app->conn[i].setup_thread != 0) {
            pthread_cancel(*app->conn[i].setup_thread);
            pthread_join(*app->conn[i].setup_thread, NULL);
        }
        conn_disconnect(&app->conn[i]);
    }

    free(app->url);

    /* Delete state file if necessary */
    if (app->ready == 1) {
        stfile_unlink(app->filename);
    }
    /* Else: Create it.. */
    else if (app->bytes_done > 0) {
        save_state(app);
    }

    print_messages(app);

    close(app->outfd);

    if (!PROTO_IS_FTP(app->conn->proto) || app->conn->proxy) {
        abuf_setup(app->conn->http->request, ABUF_FREE);
        abuf_setup(app->conn->http->headers, ABUF_FREE);
    }
    free(app->conn);
    free(app);
    free(buffer);
}

double app_gettime()
{
    struct timeval time[1];

    gettimeofday (time, NULL);

    return (double)time->tv_sec + (double)time->tv_usec / 1000000;
}

void print_messages(App *app)
{
    if (!app) {return;}

    Message *m = NULL;

    while ((m = app->message)) {
        printf("%s\n", m->text);
        app->message = m->next;
        free(m);
    }
}

char *app_size_human(char *dst, size_t len, size_t value)
{
    g_autofree char* size = g_format_size_full (value, G_FORMAT_SIZE_IEC_UNITS);

    int ret = snprintf (dst, len -1, size);

    return ret < 0 ? NULL : dst;
}


static void app_message (App* app, const char *format, ...)
{
    va_list params;

    if (!app) {
        goto nomem;
    }

    Message* m = calloc(1, sizeof(Message));
    if (!m) {
        goto nomem;
    }

    va_start(params, format);
    vsnprintf(m->text, MAX_STRING, format, params);
    va_end(params);

    if (app->message == NULL) {
        app->message = app->last_message = m;
    } else {
        app->last_message->next = m;
        app->last_message = m;
    }

    return;

nomem:
    /* Flush previous messages */
    print_messages(app);
    va_start(params, format);
    vprintf(format, params);
    va_end(params);
}

static void app_divide (App* app)
{
    /* Optimize the number of connections in case the file is small */
    off_t maxconns = max(1u, app->size / MIN_CHUNK_WORTH);
    if (maxconns < app->conf->num_connections)
        app->conf->num_connections = maxconns;

    /* Calculate each segment's size */
    off_t seg_len = app->size / app->conf->num_connections;

    if (!seg_len) {
        logd ("Too few bytes remaining, forcing a single connection");
        app->conf->num_connections = 1;
        seg_len = app->size;

        Conn *newConn = realloc(app->conn, sizeof(*app->conn));
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


static int stfile_unlink (const char *bname)
{
    char *stname = stfile_makename (bname);
    int ret = unlink(stname);
    free(stname);
    return ret;
}

static int stfile_access(const char *bname, int mode)
{
    char *stname = stfile_makename (bname);
    int ret = access(stname, mode);
    free(stname);
    return ret;
}


static int stfile_open(const char *bname, int flags, mode_t mode)
{
    char *stname = stfile_makename (bname);
    int fd = open(stname, flags, mode);
    free(stname);
    return fd;
}

static void save_state(App* app)
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
    assert(nwrite == sizeof(app->conf->num_connections));

    nwrite = write(fd, &app->bytes_done, sizeof(app->bytes_done));
    assert(nwrite == sizeof(app->bytes_done));

    for (int i = 0; i < app->conf->num_connections; i++) {
        nwrite = write(fd, &app->conn[i].currentbyte, sizeof(app->conn[i].currentbyte));
        assert(nwrite == sizeof(app->conn[i].currentbyte));
        nwrite = write(fd, &app->conn[i].lastbyte, sizeof(app->conn[i].lastbyte));
        assert(nwrite == sizeof(app->conn[i].lastbyte));
    }
    close(fd);
}

/* Thread used to set up a connection */
static void* setup_thread (void *c)
{
    Conn* conn = c;
    int oldstate;

    /* Allow this thread to be killed at any time. */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldstate);

    pthread_mutex_lock(&conn->lock);
    if (conn_setup(conn)) {
        conn->last_transfer = app_gettime();
        if (conn_exec(conn)) {
            conn->last_transfer = app_gettime();
            conn->enabled = true;
            goto out;
        }
    }

    conn_disconnect(conn);

out:
    conn->state = false;
    pthread_mutex_unlock(&conn->lock);

    return NULL;
}

static char* stfile_makename (const char *bname)
{
    const char suffix[] = ".st";
    const size_t bnameLen = strlen(bname);
    char *buf = malloc(bnameLen + sizeof(suffix));
    if (!buf) {
        perror("stfile_open");
        abort();
    }

    memcpy(buf, bname, bnameLen);
    memcpy(buf + bnameLen, suffix, sizeof(suffix));

    return buf;
}

static void reactivate_connection (App* app, int thread)
{
    /* TODO Make the minimum also depend on the connection speed */
    off_t max_remaining = MIN_CHUNK_WORTH - 1;
    int idx = -1;

    if (app->conn[thread].enabled ||
        app->conn[thread].currentbyte < app->conn[thread].lastbyte)
        return;

    for (int j = 0; j < app->conf->num_connections; j++) {
        off_t remaining =
            app->conn[j].lastbyte - app->conn[j].currentbyte;
        if (remaining > max_remaining) {
            max_remaining = remaining;
            idx = j;
        }
    }

    if (idx == -1)
        return;

    logd ("Reactivate connection %d", thread);

    app->conn[thread].lastbyte = app->conn[idx].lastbyte;
    app->conn[idx].lastbyte = app->conn[idx].currentbyte + max_remaining / 2;
    app->conn[thread].currentbyte = app->conn[idx].lastbyte;
}
