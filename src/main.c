#include <glib.h>
#include <stdio.h>
#include <gio/gio.h>

#include "log.h"
#include "thread-pool.h"

#define MAX_THREAD          30
#define PROGRESS_NAME       "graceful-downloader"


static void stop(int signal);
static void activate (GApplication* app);
static void startup (GApplication* app, gpointer udata);
static void shutdown (GApplication* app, gpointer udata);
static int command_line (GApplication* app, GApplicationCommandLine* cmd);
static gboolean show_version (const gchar* optionName, const gchar* value, gpointer data, GError** error);


static GApplication* gApp = NULL;

const GOptionEntry operation[] = {
    {"version", 'v', G_OPTION_FLAG_OPTIONAL_ARG, G_OPTION_ARG_CALLBACK, G_CALLBACK (show_version), "Display version information", NULL},
    NULL
};

int main (int argc, char *argv[])
{
    g_autofree gchar* dir = NULL;
    g_autoptr (GError) error = NULL;
    gApp = g_application_new ("org.graceful.downloader", G_APPLICATION_IS_SERVICE);

    g_signal_connect (gApp, "startup",        G_CALLBACK (startup),       NULL);
    g_signal_connect (gApp, "shutdown",       G_CALLBACK (shutdown),      NULL);
    g_signal_connect (gApp, "activate",       G_CALLBACK (activate),      NULL);
    g_signal_connect (gApp, "command-line",   G_CALLBACK (command_line),  NULL);

    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    const char* homeDir = g_get_home_dir ();
    const char* tmpDir = g_get_tmp_dir ();

    if (homeDir) {
        dir = g_strdup_printf ("%s/.log/", homeDir);
    } else {
        dir = g_strdup_printf ("%s", tmpDir);
    }

    g_set_prgname (PROGRESS_NAME);

    log_init (LOG_TYPE_FILE, LOG_DEBUG, LOG_ROTATE_TRUE, 2 << 20, dir, PROGRESS_NAME, "log");

    g_application_register (gApp, NULL, &error);
    if (error) {
        g_application_set_flags (gApp, G_APPLICATION_HANDLES_COMMAND_LINE);
        g_application_add_main_option_entries (gApp, operation);
        exit (-1);
    }

    logd ("%s daemon started ...", PROGRESS_NAME);

    g_application_hold (gApp);

    g_application_activate (gApp);

    g_application_run (gApp, argc, argv);

    return 0;
}


static void stop(int signal)
{
    g_application_release (gApp);
}

static void activate (GApplication* app)
{
    thread_pool_init (MAX_THREAD);
    logd ("created %d's threads!", MAX_THREAD);
}

static void startup (GApplication* app, gpointer udata)
{
}

static void shutdown (GApplication* app, gpointer udata)
{
    logd ("%s shutdown!", PROGRESS_NAME);
}

static int command_line (GApplication* app, GApplicationCommandLine* cmd)
{
    gint            argc = 0;
    gchar**         argv = NULL;

    g_application_hold (app);

    logi ("commandline");

    g_application_release (app);

    return 0;

}

static gboolean show_version (const gchar* optionName, const gchar* value, gpointer data, GError** error)
{
    printf ("graceful-downloader version: %d.%d.%d\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

    return TRUE;
}
