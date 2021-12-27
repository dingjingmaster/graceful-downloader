#include "dm-http.h"

bool http_init(DownloadData *d)
{
    g_return_val_if_fail (d, false);

#if 0
    Conf* conf = g_realloc (d->conf, sizeof (Conf));
    if (!conf) {
        loge ("g_realloc error: %s", strerror (errno));
        return false;
    }
    d->conf = conf;

    if (!conf_init (d->conf)) {
        loge ("conf_init error");
        return false;
    }

    Http* http = g_realloc (d->data, sizeof (Http));
    if (!http) {
        loge ("g_realloc error: %s", strerror (errno));
        return false;
    }
    d->data = http;

    Conn* conn = g_realloc_n (d->conn, d->conf->num_connections, sizeof (Conn));
    if (!conn) {
        loge ("g_realloc error: %s", strerror (errno));
        return false;
    }
    d->conn = conn;

    for (int i = 0; i < d->conf->num_connections; ++i) {
        pthread_mutex_init(&d->conn[i].lock, NULL);
    }

    g_autofree char* uri = g_uri_to_string (d->uri);
    if (d->conf->max_speed > 0) {
        /* max_speed / buffer_size < .5 */
        if (16 * d->conf->max_speed / d->conf->buffer_size < 8) {
            if (d->conf->verbose >= 2) {
                logd ("%s Buffer resized for this speed.", uri);
            }
            d->conf->buffer_size = d->conf->max_speed;
        }

        uint64_t delay = 1000000000 * d->conf->buffer_size * d->conf->num_connections / d->conf->max_speed;

        d->delayTime.tv_sec  = delay / 1000000000;
        d->delayTime.tv_nsec = delay % 1000000000;
    }

    char* buf = g_realloc (d->buf, d->conf->buffer_size);
    if (!buf) {
        loge ("g_realloc error: %s", strerror (errno));
        return false;
    }
    d->buf = buf;

    d->conn[0].conf = d->conf;
    if (!conn_set (&d->conn[0], uri)) {
        loge ("could not parse URL: %s", uri);
        d->ready = -1;
        return false;
    }

    d->conn[0].localIf = d->conf->interfaces->text;
    d->conf->interfaces = d->conf->interfaces->next;

    gf_strlcpy(d->filename, d->conn[0].file, sizeof(d->filename));
    http_decode(d->filename);

    char* s = strchr(d->filename, '?');
    if (NULL != s && d->conf->strip_cgi_parameters) {
        *s = 0;
    }

    if (*d->filename == 0) {
        gf_strlcpy(d->filename, d->conf->default_filename, sizeof(d->filename));
    }

    if (d->conf->no_clobber && access (d->filename, F_OK) == 0) {
        int ret = stfile_access (d->filename, F_OK);
        if (ret) {
            logi ("File '%s' already there; not retrieving.", d->filename);
            d->ready = -1;
            return false;
        }

        logi ("Incomplete download found, ignoring no-clobber option");
    }

    int status = 0;
    do {
        if (!conn_init(&d->conn[0])) {
            d->ready = -1;
            return false;
        }

        /* This does more than just checking the file size, it all
         * depends on the protocol used. */
        status = conn_info(&d->conn[0]);
        if (!status) {
            char msg[80];
            int code = conn_info_status_get(msg, sizeof(msg), d->conn);
            loge ("ERROR %d: %s.", code, msg);
            d->ready = -1;
            return false;
        }
    } while (status == -1);

    d->size = d->conn[0].size;
    if (d->conf->verbose > 0) {
        if (d->size != LLONG_MAX) {
            g_autofree char* size = g_format_size_full (d->size, G_FORMAT_SIZE_IEC_UNITS);
            logd ("%s File size: %s (%jd bytes)", uri, size, d->size);
        } else {
            loge ("%s File size: unavailable", uri);
        }
    }

    /* Wildcards in URL --> Get complete filename */
    if (d->filename[strcspn(d->filename, "*?")])
        gf_strlcpy(d->filename, d->conn[0].file, sizeof(d->filename));

    if (*d->conn[0].outputFilename != 0) {
        gf_strlcpy(d->filename, d->conn[0].outputFilename, sizeof (d->filename));
    }

    int fd = -1;
    ssize_t nread;
    d->outfd = -1;
    if (!d->conn[0].supported) {
        logd ("%s Server unsupported, starting from scratch with one connection.", uri);
        d->conf->num_connections = 1;
        void *newConn = realloc(d->conn, sizeof(Conn));
        if (!newConn) {
            return false;
        }

        d->conn = newConn;
        download_divide (d);
    } else if ((fd = stfile_open(d->filename, O_RDONLY, 0)) != -1) {
        int oldFormat = 0;
        off_t stsize = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);

        nread = read (fd, &d->conf->num_connections, sizeof (d->conf->num_connections));
        if (nread != sizeof(d->conf->num_connections)) {
            loge ("%s.st: Error, truncated state file", d->filename);
            close(fd);
            return false;
        }

        if (d->conf->num_connections < 1) {
            loge ("Bogus number of connections stored in state file");
            close(fd);
            return false;
        }

        if (stsize < (off_t) (sizeof (d->conf->num_connections) +
                sizeof(d->bytesDone) + 2 * d->conf->num_connections * sizeof (d->conn[0].currentbyte))) {
            /* FIXME this might be wrong, the file may have been
             * truncated, we need another way to check. */
            logd ("State file has old format.");
            oldFormat = 1;
        }

        void *newConn = realloc(d->conn, sizeof(Conn) * d->conf->num_connections);
        if (!newConn) {
            close(fd);
            return false;
        }
        d->conn = newConn;

        memset(d->conn + 1, 0, sizeof(Conn) * (d->conf->num_connections - 1));

        if (oldFormat) {
            download_divide(d);
        }

        nread = read (fd, &d->bytesDone, sizeof (d->bytesDone));
        g_return_val_if_fail (nread == sizeof (d->bytesDone), false);
        for (int i = 0; i < d->conf->num_connections; ++i) {
            nread = read(fd, &d->conn[i].currentbyte, sizeof(d->conn[i].currentbyte));
            g_return_val_if_fail (nread == sizeof(d->conn[i].currentbyte), false);
            if (!oldFormat) {
                nread = read(fd, &d->conn[i].lastbyte, sizeof (d->conn[i].lastbyte));
                g_return_val_if_fail (nread == sizeof (d->conn[i].lastbyte), false);
            }
        }

        logi ("%s State file found: %jd bytes downloaded, %jd to go.", uri, d->bytesDone, d->size - d->bytesDone);

        close(fd);

        if ((d->outfd = open(d->filename, O_WRONLY, 0666)) == -1) {
            loge ("%s Error opening local file", uri);
            return false;
        }
    }

    /* If outfd == -1 we have to start from scrath now */
    if (d->outfd == -1) {
        download_divide(d);

        if ((d->outfd = open(d->filename, O_CREAT | O_WRONLY, 0666)) == -1) {
            loge ("%s Error opening local file", uri);
            return 0;
        }

        /* And check whether the filesystem can handle seeks to
           past-EOF areas.. Speeds things up. :) AFAIK this
           should just not happen: */
        if (lseek(d->outfd, d->size, SEEK_SET) == -1 &&
            d->conf->num_connections > 1) {
            /* But if the OS/fs does not allow to seek behind
               EOF, we have to fill the file with zeroes before
               starting. Slow.. */
            logi ("%i Crappy filesystem/OS.. Working around. :-(", uri);
            lseek(d->outfd, 0, SEEK_SET);
            memset(d->buf, 0, d->conf->buffer_size);
            off_t j = d->size;
            while (j > 0) {
                ssize_t nwrite;

                if ((nwrite = write(d->outfd, d->buf, min(j, d->conf->buffer_size))) < 0) {
                    if (errno == EINTR || errno == EAGAIN) {
                        continue;
                    }
                    loge ("%s Error creating local file", uri);
                    return false;
                }
                j -= nwrite;
            }
        }
    }

    for (int i = 0; i < d->conf->num_connections; ++i) {
        conn_set (&d->conn[i], uri);
        d->conn[i].localIf = d->conf->interfaces->text;
        d->conf->interfaces = d->conf->interfaces->next;
        d->conn[i].conf = d->conf;
        if (i) {
            d->conn[i].supported = true;
        }
    }

    for (int i = 0; i < d->conf->num_connections; i++) {
        if (d->conn[i].currentbyte >= d->conn[i].lastbyte) {
            pthread_mutex_lock(&d->conn[i].lock);
            reactivate_connection (d, i);
            pthread_mutex_unlock(&d->conn[i].lock);
        } else if (d->conn[i].currentbyte < d->conn[i].lastbyte) {
            if (d->conf->verbose >= 2) {
                logd ("%s Connection %i downloading from %s:%i using interface %s", uri,
                    i, d->conn[i].host, d->conn[i].port, d->conn[i].localIf);
            }

            d->conn[i].state = true;
            if (pthread_create (d->conn[i].setupThread, NULL, setup_thread, &d->conn[i]) != 0) {
                loge ("%s pthread error!!!", uri);
                d->ready = -1;
            }
        }
    }

    d->startTime = gf_gettime();
    d->ready = 0;
#endif
    return true;
}

bool http_download(DownloadData *d)
{
    g_return_val_if_fail (d && d->data, false);

#if 0
    Http* http = (Http*) d->data;

    g_autofree char* uri = g_uri_to_string (d->uri);


    fd_set fds[1];
    int hifd, i;
    off_t remaining, size;
    struct timeval timeval[1];
    Url *url_ptr;
    struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000};
    unsigned long long int max_speed_ratio;

    /* Create statefile if necessary */
    if (gf_gettime() > d->nextState) {
        save_state(d);
        d->nextState = gf_gettime() + d->conf->save_state_interval;
    }

    /* Wait for data on (one of) the connections */
    FD_ZERO(fds);
    hifd = 0;
    for (i = 0; i < d->conf->num_connections; i++) {
        /* skip connection if setup thread hasn't released the lock yet */
        if (!pthread_mutex_trylock(&d->conn[i].lock)) {
            if (d->conn[i].enabled) {
                FD_SET(d->conn[i].tcp->fd, fds);
                hifd = max(hifd, d->conn[i].tcp->fd);
            }
            pthread_mutex_unlock(&d->conn[i].lock);
        }
    }

    if (hifd == 0) {
        /* No connections yet. Wait... */
        if (gf_sleep(delay) < 0) {
            loge ("%s Error while waiting for connection: %s", strerror(errno), uri);
            d->ready = -1;
            return false;
        }

        goto conn_check;
    }

    timeval->tv_sec = 0;
    timeval->tv_usec = 100000;
    if (select(hifd + 1, fds, NULL, NULL, timeval) == -1) {
        /* A select() error probably means it was interrupted
         * by a signal, or that something else's very wrong... */
        d->ready = -1;
        return false;
    }

    /* Handle connections which need attention */
    for (i = 0; i < d->conf->num_connections; i++) {
        /* skip connection if setup thread hasn't released the lock yet */
        if (pthread_mutex_trylock(&d->conn[i].lock))
            continue;

        if (!d->conn[i].enabled)
            goto next_conn;

        if (!FD_ISSET(d->conn[i].tcp->fd, fds)) {
            time_t timeout = d->conn[i].lastTransfer + d->conf->connection_timeout;
            if (gf_gettime() > timeout) {
                if (d->conf->verbose) {
                    loge ("%s Connection %i timed out", uri, i);
                }
                conn_disconnect (&d->conn[i]);
            }
            goto next_conn;
        }

        d->conn[i].lastTransfer = gf_gettime();
        size = tcp_read(d->conn[i].tcp, d->buf, d->conf->buffer_size);
        if (size == -1) {
            if (d->conf->verbose) {
                loge("%s Error on connection %i! Connection closed", uri, i);
            }
            conn_disconnect(&d->conn[i]);
            goto next_conn;
        }

        if (size == 0) {
            if (d->conf->verbose) {
                /* Only abnormal behaviour if: */
                if (d->conn[i].currentbyte < d->conn[i].lastbyte && d->size != LLONG_MAX) {
                    loge ("%s Connection %i unexpectedly closed", uri, i);
                } else {
                    logi ("%s Connection %i finished", uri, i);
                }
            }

            if (!d->conn[0].supported) {
                d->ready = 1;
            }

            conn_disconnect(&d->conn[i]);
            reactivate_connection(d, i);
            goto next_conn;
        }

        /* remaining == Bytes to go */
        remaining = d->conn[i].lastbyte - d->conn[i].currentbyte;
        if (remaining < size) {
            if (d->conf->verbose) {
                logi ("%s Connection %i finished", uri, i);
            }
            conn_disconnect(&d->conn[i]);
            size = remaining;
            /* Don't terminate, still stuff to write! */
        }

        /* This should always succeed.. */
        lseek (d->outfd, d->conn[i].currentbyte, SEEK_SET);
        if (write(d->outfd, d->buf, size) != size) {
            loge ("%s Write error!", uri);
            d->ready = -1;
            pthread_mutex_unlock(&d->conn[i].lock);
            return false;
        }

        d->conn[i].currentbyte += size;
        d->bytesDone += size;
        if (remaining == size) {
            reactivate_connection(d, i);
        }

    next_conn:
        pthread_mutex_unlock(&d->conn[i].lock);
    }

    if (d->ready) {
        return true;
    }

conn_check:
    /* Look for aborted connections and attempt to restart them. */
    for (i = 0; i < d->conf->num_connections; i++) {
        /* skip connection if setup thread hasn't released the lock yet */
        if (pthread_mutex_trylock(&d->conn[i].lock))
            continue;

        if (!d->conn[i].enabled && d->conn[i].currentbyte < d->conn[i].lastbyte) {
            if (!d->conn[i].state) {
                // Wait for termination of this thread
                pthread_join(*(d->conn[i].setupThread), NULL);

                conn_set(&d->conn[i], url_ptr->text);
                url_ptr = url_ptr->next;
                /* app->conn[i].local_if = app->conf->interfaces->text;
                   app->conf->interfaces = app->conf->interfaces->next; */
                if (d->conf->verbose >= 2)
                    logd ("%s Connection %i downloading from %s:%i using interface %s",
                        uri, i, d->conn[i].host, d->conn[i].port, d->conn[i].localIf);

                d->conn[i].state = true;
                if (pthread_create (d->conn[i].setupThread, NULL, setup_thread, &d->conn[i]) == 0) {
                    d->conn[i].lastTransfer = gf_gettime();
                } else {
                    loge ("%s pthread error!!!", uri);
                    d->ready = -1;
                }
            } else {
                if (gf_gettime() > (d->conn[i].lastTransfer +
                        d->conf->reconnect_delay)) {
                    pthread_cancel(*d->conn[i].setupThread);
                    d->conn[i].state = false;
                    pthread_join(*d->conn[i].setupThread, NULL);
                }
            }
        }
        pthread_mutex_unlock(&d->conn[i].lock);
    }

    /* Calculate current average speed and finish_time */
    d->bytesPerSecond = (off_t)((double)(d->bytesDone - d->startByte) / (gf_gettime() - d->startTime));
    if (d->bytesPerSecond != 0) {
        d->finishTime = (int)(d->startTime + (double)(d->size - d->startByte) / d->bytesPerSecond);
    } else {
        d->finishTime = INT_MAX;
    }

    /* Check speed. If too high, delay for some time to slow things
       down a bit. I think a 5% deviation should be acceptable. */
    if (d->conf->max_speed > 0) {
        max_speed_ratio = 1000 * d->bytesPerSecond / d->conf->max_speed;
        if (max_speed_ratio > 1050) {
            d->delayTime.tv_nsec += 10000000;
            if (d->delayTime.tv_nsec >= 1000000000) {
                d->delayTime.tv_sec++;
                d->delayTime.tv_nsec -= 1000000000;
            }
        } else if (max_speed_ratio < 950) {
            if (d->delayTime.tv_nsec >= 10000000) {
                d->delayTime.tv_nsec -= 10000000;
            } else if (d->delayTime.tv_sec > 0) {
                d->delayTime.tv_sec--;
                d->delayTime.tv_nsec += 999000000;
            } else {
                d->delayTime.tv_sec = 0;
                d->delayTime.tv_nsec = 0;
            }
        }
        if (gf_sleep(d->delayTime) < 0) {
            loge ("%s Error while enforcing throttling: %s", uri, strerror(errno));
            d->ready = -1;
            return false;
        }
    }

    /* Ready? */
    if (d->bytesDone == d->size)
        d->ready = 1;
#endif
    return true;
}

void http_free(DownloadData *d)
{
    //    http_disconnect ();

}
