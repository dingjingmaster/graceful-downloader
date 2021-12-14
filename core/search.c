#include "search.h"

#include "conn.h"
#include "utils.h"
#include "config.h"

static void *search_speedtest      (void *r);
static int   search_sortlist_qsort (const void *a, const void *b);

int search_makelist (Search* results, char *orig_url)
{
    int size = 8192;
    Conn conn[1];
    double t;
    const char *start, *end;

    memset(conn, 0, sizeof(Conn));

    conn->conf = results->conf;
    t = gf_gettime();
    if (!conn_set(conn, orig_url) || !conn_init(conn) || !conn_info(conn))
        return -1;

    size_t orig_len = strlcpy(results[0].url, orig_url,
        sizeof(results[0].url));
    results[0].speed = 1 + 1000 * (gf_gettime() - t);
    results[0].size = conn->size;
    int nresults = 1;

    char *s = malloc(size);
    if (!s)
        return 1;

    /* TODO improve matches */
    snprintf(s, size, "http://www.filesearching.com/cgi-bin/s?"
                      "w=a&" /* TODO describe */
                      "x=15&y=15&" /* TODO describe */
                      /* Size in bytes:   */ "s=on&"
                      /* Exact search:    */ "e=on&"
                      /* Language:        */ "l=en&"
                      /* Search Type:     */ "t=f&"
                      /* Sorting:         */ "o=n&"
                      /* Filename:        */ "q=%s&"
                      /* Num. of results: */ "m=%i&"
                      /* Size (min/max):  */ "s1=%jd&s2=%jd",
        conn->file, results->conf->search_amount,
        conn->size, conn->size);

    conn_disconnect(conn);
    memset(conn, 0, sizeof(Conn));
    conn->conf = results->conf;

    if (!conn_set(conn, s)) {
        goto done;
    }

    {
        pthread_mutex_unlock(&conn->lock);
        int tmp = conn_setup(conn);
        pthread_mutex_unlock(&conn->lock);
        if (!tmp || !conn_exec(conn))
            goto done;
    }

    {
        int j = 0;

        for (int i; (i = tcp_read(conn->tcp, s + j, size - j)) > 0;) {
            j += i;
            if (j + 10 >= size) {
                size *= 2;
                char *tmp = realloc(s, size);
                if (!tmp)
                    goto done;
                s = tmp;
                memset(s + size / 2, 0, size / 2);
            }
        }
        s[j] = '\0';
    }

    conn_disconnect(conn);

    start = strstr(s, "<pre class=list");
    if (!start)
        goto done;
    end = strstr(start, "</pre>");
    /* Incomplete list */
    if (!end)
        goto done;

    for (const char *url, *eol;
         start < end && nresults < results->conf->search_amount;
         start = eol + 1) {
        eol = strchr(start, '\n');
        if (eol > end || !eol)
            eol = end;
        do {
            url = start;
            start = strstr(start, "<a href=") + 8;
        } while (start < eol);

        /* Check it isn't the original URL */
        if (!strncmp(url, orig_url, orig_len))
            continue;

        strlcpy(results[nresults].url, url, sizeof(results[0].url));
        results[nresults].size = results[0].size;
        results[nresults].conf = results->conf;
        ++nresults;
    }

done:
    free(s);
    return nresults;
}

enum
{
    SPEED_ACTIVE  = -3,
    SPEED_FAILED  = -2,
    SPEED_DONE    = -1,
    SPEED_PENDING =  0,
    /* Or > 0 */
};

int search_getspeeds(Search* results, int count)
{
    const struct timespec delay = {.tv_nsec = 10000000};

    int left = count, correct = 0;
    for (int i = 0; i < count; i++) {
        if (results[i].speed) {
            results[i].speed_start_time = 0;
            left--;
            if (results[i].speed > 0)
                correct++;
        }
    }

    for (int running = 0; left > 0; gf_sleep(delay)) {
        for (int i = 0; i < count; i++) {
            switch (results[i].speed) {
            case SPEED_ACTIVE:
                if (gf_gettime() < results[i].speed_start_time
                        + results->conf->search_timeout)
                    continue; // not timed out yet
                pthread_cancel(*results[i].speed_thread);
                break; // do the bookkeeping
            case SPEED_FAILED:
                break; // do the bookkeeping
            case SPEED_DONE:
                continue; // already processed
            case SPEED_PENDING:
                if (running >= results->conf->search_threads)
                    continue; // running too many, skip
                results[i].speed = SPEED_ACTIVE;
                results[i].speed_start_time = gf_gettime();
                if (pthread_create(results[i].speed_thread,
                        NULL, search_speedtest,
                        &results[i]) == 0)
                    running++;
                else
                    results[i].speed = SPEED_PENDING;
                continue; // go to the next item
            default:
                if (!results[i].speed_start_time)
                    continue; // already processed
                // do the bookkeeping
            }
            // The thread must have been cancelled or finished
            pthread_join(*results[i].speed_thread, NULL);
            running--;
            left--;
            switch (results[i].speed) {
            case SPEED_ACTIVE:
            case SPEED_FAILED:
                results[i].speed = SPEED_DONE;
                break;
            default:
                results[i].speed_start_time = 0;
                if (results[i].speed > 0)
                    correct++;
            }
        }
    }

    return correct;
}

static void * search_speedtest (void *r)
{
    Search *results = r;
    Conn conn[1];
    int oldstate;

    /* Allow this thread to be killed at any time. */
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldstate);

    memset(conn, 0, sizeof(Conn));
    conn->conf = results->conf;
    if (conn_set(conn, results->url)
        && conn_init(conn)
        && conn_info(conn)
        && conn->size == results->size)
        /* Add one because it mustn't be zero */
        results->speed = 1 + 1000 * (gf_gettime() - results->speed_start_time);
    else
        results->speed = SPEED_FAILED;

    conn_disconnect(conn);

    return NULL;
}


void search_sortlist(Search *results, int count)
{
    qsort(results, count, sizeof(Search), search_sortlist_qsort);
}

static int search_sortlist_qsort(const void *a, const void *b)
{
    if (((Search *) a)->speed < 0 && ((Search *) b)->speed > 0) {
        return 1;
    }

    if (((Search *) a)->speed > 0 && ((Search *) b)->speed < 0) {
        return -1;
    }

    if (((Search *) a)->speed < ((Search *) b)->speed) {
        return -1;
    } else {
        return ((Search *) a)->speed > ((Search *) b)->speed;
    }
}
