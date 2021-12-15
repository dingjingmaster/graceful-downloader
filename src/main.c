#include <glib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <gio/gio.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "log.h"
#include "global.h"
#include "thread-pool.h"

#define MAX_THREAD                  30
#define SEM_KEY                     202112
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
    char                    uri[MAX_STRING];
};

struct _Main
{
    bool                    isPrimary;

    int                     semID;
    int                     shmID;
    MainMessage*            shmPtr;

    MainMessage             message;
};



static void init ();
static void destory ();
static void stop(int signal);
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

    g_autofree gchar* dir = NULL;
    g_autoptr (GError) error = NULL;

    g_set_prgname (PROGRESS_NAME);

    const char* homeDir = g_get_home_dir ();
    const char* tmpDir = g_get_tmp_dir ();

    if (homeDir) {
        dir = g_strdup_printf ("%s/.log/", homeDir);
    } else {
        dir = g_strdup_printf ("%s", tmpDir);
    }

    log_init (LOG_TYPE_FILE, LOG_DEBUG, LOG_ROTATE_TRUE, 2 << 20, dir, PROGRESS_NAME, "log");

    while (1) {
        sleep (1);
    }

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

    // sem
    int semID = semget ((key_t) SEM_KEY, 1, 0666 | IPC_CREAT);
    if (semID < 0) {
        printf ("semget error: %s\n", strerror (errno));
        destory ();
        exit (-1);
    }

    int shmID = shm_open (PROGRESS_NAME, O_RDWR, 00777);
    if (shmID < 0) {
        shmID = shm_open (PROGRESS_NAME, O_RDWR | O_CREAT, 00777);
        if (shmID < 0) {
            printf ("shm_open error: %s\n", strerror (errno));
            destory ();
            exit (-1);
        } else {
            gMain->isPrimary = true;
        }
    }

    if (shmID < 0) {
        printf ("shmopen error: %s\n", strerror (errno));
        destory ();
        exit (-1);
    }

    gMain->semID = semID;
    gMain->shmID = shmID;

    int ret = ftruncate (gMain->shmID, sizeof (MainMessage));
    if (ret < 0) {
        printf ("ftruncate fail, %s\n", strerror (errno));
        destory ();
    }

    void* addr = mmap (NULL, sizeof (MainMessage), PROT_READ | PROT_WRITE, MAP_SHARED, gMain->shmID, SEEK_SET);
    if (NULL == addr) {
        printf ("mmap address fail! %s\n", strerror (errno));
        destory ();
    }

    gMain->shmPtr = (MainMessage*) addr;
}

static void destory ()
{
    if (!gMain) return;

    if (gMain->shmPtr) {
        munmap (gMain->shmPtr, sizeof (MainMessage));
    }

    if (gMain->isPrimary) {
        shm_unlink (PROGRESS_NAME);
        semctl (gMain->semID, 0, IPC_RMID, NULL);
    }

    gMain->shmPtr = NULL;
}

static void stop(int signal)
{
    destory ();
}


static gboolean show_version (const gchar* optionName, const gchar* value, gpointer data, GError** error)
{
    printf ("graceful-downloader version: %d.%d.%d\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

    return TRUE;
}
