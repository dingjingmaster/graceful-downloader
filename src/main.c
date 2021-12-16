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

#define MAX_THREAD                  30
#define SEM_KEY                     202112
#define COMMANDLINE_BUF             102400
#define PROGRESS_NAME               "graceful-downloader"

typedef struct _Main                Main;
typedef enum _MAIN_OPERATION        MAIN_OPERATION;
typedef struct _MainMessage         MainMessage;

enum _MAIN_OPERATION
{
    MAIN_MESSAGE_NONE,
    MAIN_MESSAGE_ADD_URI,
};

struct _MainMessage
{
    MAIN_OPERATION          op;
    char                    buf[COMMANDLINE_BUF];
};

struct _Main
{
    bool                    isPrimary;
    bool                    shutdown;

    sem_t*                  sem;

    int                     shmID;
    MainMessage*            shmPtr;

    GMainLoop*              mainLoop;

    // command line thread
    pthread_t               commandLine;
};


static void init ();
static void destory ();
static void stop(int signal);
static void start_routine (void* data);
static gboolean show_version (const gchar* optionName, const gchar* value, gpointer data, GError** error);


static Main* gMain = NULL;
static GApplication* gApp = NULL;

const GOptionEntry operation[] = {
    {"version", 'v', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, G_CALLBACK (show_version), "Display version information", NULL},
    NULL
};


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
        MainMessage msg;
        strncpy (msg.buf, cmd, sizeof (msg.buf));
        memcpy (gMain->shmPtr, &msg, sizeof (MainMessage));

        printf ("cmd: %s\n", cmd);
        sem_post (gMain->sem);

        exit (0);
    }

    int ret = pthread_create (&(gMain->commandLine), NULL, (void*) start_routine, NULL);
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

    log_init (LOG_TYPE_FILE, LOG_DEBUG, LOG_ROTATE_TRUE, 2 << 20, dir, PROGRESS_NAME, "log");

    g_main_loop_run (gMain->mainLoop);


    destory ();

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
    printf ("is:%d\n", gMain->isPrimary);

    int semFlag = O_RDWR;
    int shmFlag = O_RDWR;
    if (gMain->isPrimary) {
        semFlag |= (O_CREAT | O_EXCL);
        shmFlag |= (O_CREAT | O_EXCL);
    }
    sem_t* sem = sem_open (PROGRESS_NAME, semFlag, 0777, 1);
    if (SEM_FAILED == sem) {
        printf ("sem_open error: %s\n", strerror (errno));
        destory ();
        exit (-1);
    }
    gMain->sem = sem;

    // shm
    int shmID = shm_open (PROGRESS_NAME, shmFlag, 0777);
    if (shmID < 0) {
        printf ("shm_open error: %s\n", strerror (errno));
        destory ();
        exit (-1);
    }
    gMain->shmID = shmID;

    int ret = ftruncate (gMain->shmID, sizeof (MainMessage));
    if (ret < 0) {
        printf ("ftruncate fail, %s\n", strerror (errno));
        destory ();
        exit (-1);
    }

    void* addr = mmap (NULL, sizeof (MainMessage), PROT_READ | PROT_WRITE, MAP_SHARED, gMain->shmID, SEEK_SET);
    if (NULL == addr) {
        printf ("mmap address fail! %s\n", strerror (errno));
        destory ();
        exit (-1);
    }

    gMain->shmPtr = (MainMessage*) addr;

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

    if (gMain->shmPtr) {
        munmap (gMain->shmPtr, sizeof (MainMessage));
    }

    if (gMain->isPrimary) {
        shm_unlink (PROGRESS_NAME);
        sem_unlink (PROGRESS_NAME);
    }

    gMain->shmPtr = NULL;

    if (gMain->mainLoop && g_main_loop_is_running (gMain->mainLoop)) {
        g_main_loop_quit (gMain->mainLoop);
    }
}

static void stop(int signal)
{
    if (g_main_loop_is_running (gMain->mainLoop)) {
        g_main_loop_quit (gMain->mainLoop);
    }

    if (gMain->isPrimary) {
        sem_post (gMain->sem);
    }

    destory ();
}

static void start_routine (void* data)
{
    //
    while (!gMain->shutdown) {
        sem_wait (gMain->sem);

        logd ("get option: %d -- %s", gMain->shmPtr->op, (MainMessage*) (gMain->shmPtr)->buf);

        // parse command line

        // write back

//        if (!sem_v ()) {
//            loge ("sem_v error");
//            break;
//        }
    }
}


static gboolean show_version (const gchar* optionName, const gchar* value, gpointer data, GError** error)
{
    printf ("graceful-downloader version: %d.%d.%d\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

    return TRUE;
}
