#include <glib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <gio/gio.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <semaphore.h>

#include "log.h"
#include "global.h"
#include "thread-pool.h"

#define MAX_THREAD                  10
#define SEM_KEY                     202112
#define COMMANDLINE_BUF             10240
#define PROGRESS_NAME               "graceful-downloader"

typedef struct _Main                Main;
typedef struct _MainMessage         MainMessage;
typedef enum _MainMessageOp         MainMessageOp;

enum _MainMessageOp
{
    MAIN_MESSAGE_NONE,
    MAIN_MESSAGE_CLIENT,
    MAIN_MESSAGE_SERVICE,
};

struct _MainMessage
{
    MainMessageOp           op;
    char                    buf[COMMANDLINE_BUF];
};

struct _Main
{
    bool                    isPrimary;
    bool                    shutdown;

    sem_t*                  clientSem;
    sem_t*                  serviceSem;

    char*                   clientBuf;
    char*                   serverBuf;

    GMainLoop*              mainLoop;

    // command line thread
    pthread_t               commandLine;
};


static void init ();
static void destory ();
static void stop(int signal);
static void* start_routine (void* data);


static Main* gMain = NULL;
static GApplication* gApp = NULL;


int main (int argc, char *argv[])
{
    init ();

    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    signal(SIGKILL, stop);

    g_autofree gchar* dir = NULL;
    g_autoptr (GError) error = NULL;

    g_set_prgname (PROGRESS_NAME);

    // start command line detail
    if (!gMain->isPrimary) {
        g_autofree char* cmd = g_strjoinv ("|", argv);
        memcpy (gMain->clientBuf, cmd, COMMANDLINE_BUF - 1);
        sem_post (gMain->serviceSem);

        //
        sem_wait (gMain->clientSem);
        printf ("%s\n", gMain->serverBuf);

        memset (gMain->clientBuf, 0, COMMANDLINE_BUF);
        memset (gMain->serverBuf, 0, COMMANDLINE_BUF);

        exit (0);
    }

    int ret = pthread_create (&(gMain->commandLine), NULL, start_routine, NULL);
    if (0 != ret) {
        printf ("pthread_create error: %s\n", strerror (errno));
        destory ();
        exit (-1);
    }

    const char* homeDir = g_get_home_dir ();
    const char* tmpDir = g_get_tmp_dir ();

    if (homeDir) {
        dir = g_strdup_printf ("%s/.log/", homeDir);
    } else {
        dir = g_strdup_printf ("%s", tmpDir);
    }

    log_init (LOG_TYPE_FILE, LOG_DEBUG, LOG_ROTATE_FALSE, 2 << 30, dir, PROGRESS_NAME, "log");
    logi ("%s is starting ...", PROGRESS_NAME);

    // start main
    ret = thread_pool_init (MAX_THREAD);
    if (0 != ret) {
        printf ("thread_pool_init error!\n");
        destory ();
        exit (-1);
    }

    g_main_loop_run (gMain->mainLoop);

    destory ();

    logi ("%s stoped!", PROGRESS_NAME);

    return 0;
}


static void init ()
{
    gMain = g_malloc0 (sizeof (Main));
    if (!gMain) {
        printf ("Not enough RAM!\n");
        destory ();
        exit (-1);
    }

    char tbuf[32] = {0};
    gMain->isPrimary = true;
    FILE* fp = popen ("pidof " PROGRESS_NAME, "r");
    if (NULL != fp) {
        if (NULL != fgets (tbuf, sizeof (tbuf) - 1, fp)) {
            gchar** arr = g_strsplit (tbuf, " ", -1);
            if (strlen (tbuf) > 0 && g_strv_length (arr) > 1) {
                gMain->isPrimary = false;
            }
            if (arr) g_strfreev (arr);
        }
        pclose (fp);
    } else {
        gMain->isPrimary = false;
        printf ("popen error!\n");
        destory ();
        exit (-1);
    }

    int semFlag = O_RDWR;
    int shmFlag = O_RDWR;
    mode_t mask = umask (0);
    if (gMain->isPrimary) {
        semFlag |= (O_CREAT | O_EXCL);
        shmFlag |= (O_CREAT | O_EXCL);
    }


    for (int i = 0; i < 2; ++i) {
        // sem
        g_autofree char* key = g_strdup_printf ("%s_%d", PROGRESS_NAME, i);
        sem_t* sem = sem_open (key, semFlag, 0700, 0);
        if (SEM_FAILED == sem) {
            printf ("sem_open error: %s\n", strerror (errno));
            umask (mask);
            destory ();
            exit (-1);
        }

        switch (i) {
        case 0:
            gMain->clientSem = sem;
            break;
        case 1:
            gMain->serviceSem = sem;
        }

        // shm
        int shmID = shm_open (key, shmFlag, 0700);
        if (shmID < 0) {
            printf ("shm_open error: %s\n", strerror (errno));
            umask (mask);
            destory ();
            exit (-1);
        }
        umask (mask);

        if (gMain->isPrimary) {
            int ret = ftruncate (shmID, COMMANDLINE_BUF);
            if (ret < 0) {
                printf ("ftruncate fail, %s\n", strerror (errno));
                destory ();
                exit (-1);
            }
        }

        void* addr = mmap (NULL, COMMANDLINE_BUF, PROT_READ | PROT_WRITE, MAP_SHARED, shmID, SEEK_SET);
        if (MAP_FAILED == addr) {
            printf ("mmap address fail! %s\n", strerror (errno));
            destory ();
            exit (-1);
        }

        switch (i) {
        case 0:
            gMain->clientBuf = addr;
            break;
        case 1:
            gMain->serverBuf = addr;
        }
    }


    gMain->mainLoop = g_main_loop_new (NULL, true);
    if (!gMain->mainLoop) {
        printf ("main loop fail!\n");
        destory ();
        exit (-1);
    }
}

static void destory ()
{
    if (!gMain) return;

    gMain->shutdown = true;

    if (gMain->commandLine > 0) {
        pthread_join (gMain->commandLine, NULL);
    }    

    if (gMain->clientBuf) {
        munmap (gMain->clientBuf, sizeof (COMMANDLINE_BUF));
    }
    gMain->clientBuf = NULL;

    if (gMain->serverBuf) {
        munmap (gMain->serverBuf, sizeof (COMMANDLINE_BUF));
    }
    gMain->serverBuf = NULL;

    if (gMain->isPrimary) {
        for (int i = 0; i < 2; ++i) {
            g_autofree char* key = g_strdup_printf ("%s_%d", PROGRESS_NAME, i);
            shm_unlink (key);
            sem_unlink (key);
        }

        thread_pool_destory();
    }

    if (gMain->mainLoop && g_main_loop_is_running (gMain->mainLoop)) {
        g_main_loop_quit (gMain->mainLoop);
    }
}

static void stop(int signal)
{
    if (gMain->isPrimary) {
        sem_post (gMain->serviceSem);
    }

    if (gMain->mainLoop && g_main_loop_is_running (gMain->mainLoop)) {
        g_main_loop_quit (gMain->mainLoop);
    }
}

static void* start_routine (void* data)
{
    // help
    g_autofree char* help =
        g_strdup_printf ("Usage: %s [options] [url1] [uri2] [url...]\n"
                        "\n"
                        "  -h\tThis information\n"
                        "  -v\tVersion information\n", PROGRESS_NAME);

    // version
    g_autofree char* version =
        g_strdup_printf ("%s versio: %d.%d.%d\n", PROGRESS_NAME, VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

    while (!gMain->shutdown) {
        sem_wait (gMain->serviceSem);

        logd ("get option >> c: %s -- s: %s", gMain->clientBuf, gMain->serverBuf);

        // parse command line
        bool find = false;
        char** arr = g_strsplit (gMain->clientBuf, "|", -1);
        int len = g_strv_length (arr);
        for (int i = 0; i < len; ++i) {
            if (arr[i] && g_str_has_prefix (arr[i], "-")) {
                if (0 == g_ascii_strcasecmp ("-h", arr[i])) {
                    int min = min (strlen (help), COMMANDLINE_BUF - 1);
                    memset (gMain->serverBuf, 0, COMMANDLINE_BUF);
                    memcpy (gMain->serverBuf, help, min);
                    find = true;
                    sem_post (gMain->clientSem);
                    break;
                } else if (0 == g_ascii_strcasecmp ("-v", arr[i])) {
                    int min = min (strlen (version), COMMANDLINE_BUF - 1);
                    memset (gMain->serverBuf, 0, COMMANDLINE_BUF);
                    memcpy (gMain->serverBuf, version, min);
                    find = true;
                    sem_post (gMain->clientSem);
                    break;
                }
            }
        }

        if (!find) {
            int min = min (strlen (help), COMMANDLINE_BUF - 1);
            memset (gMain->serverBuf, 0, COMMANDLINE_BUF);
            memcpy (gMain->serverBuf, help, min);
            sem_post (gMain->clientSem);
        }
    }

    return NULL;
}
