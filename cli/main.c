/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mm-cli -- Control modem status & access information from the command line
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include "libmm-glib.h"

#define PROGRAM_NAME    "mmcli"
#define PROGRAM_VERSION PACKAGE_VERSION

/* Globals */
static GMainLoop *loop;
static gboolean keep_loop;
static GCancellable *cancellable;

/* Context */
static gboolean version_flag;
static gboolean async_flag;
static gboolean list_modems_flag;
static gboolean monitor_modems_flag;
static gboolean scan_modems_flag;
static gchar *set_logging_str;

static GOptionEntry entries[] = {
    { "version", 'V', 0, G_OPTION_ARG_NONE, &version_flag,
      "Print version",
      NULL
    },
    { "set-logging", 'L', 0, G_OPTION_ARG_STRING, &set_logging_str,
      "Set logging level in the ModemManager daemon",
      "[ERR,WARN,INFO,DEBUG]",
    },
    { "async", 'a', 0, G_OPTION_ARG_NONE, &async_flag,
      "Use asynchronous methods",
      NULL
    },
    { "list-modems", 'l', 0, G_OPTION_ARG_NONE, &list_modems_flag,
      "List available modems",
      NULL
    },
    { "monitor-modems", 'm', 0, G_OPTION_ARG_NONE, &monitor_modems_flag,
      "List available modems and monitor additions and removals",
      NULL
    },
    { "scan-modems", 's', 0, G_OPTION_ARG_NONE, &scan_modems_flag,
      "Request to re-scan looking for modems",
      NULL
    },
    { NULL }
};

static void
signals_handler (int signum)
{
    if (cancellable) {
        /* Ignore consecutive requests of cancellation */
        if (!g_cancellable_is_cancelled (cancellable)) {
            g_printerr ("%s\n",
                        "cancelling the operation...");
            g_cancellable_cancel (cancellable);
        }
        return;
    }

    if (loop &&
        g_main_loop_is_running (loop)) {
        g_printerr ("%s\n",
                    "cancelling the main loop...");
        g_main_loop_quit (loop);
    }
}

static void
print_version_and_exit (void)
{
    g_print ("\n"
             PROGRAM_NAME " " PROGRAM_VERSION "\n"
             "Copyright (2011) Aleksander Morgado\n"
             "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl-2.0.html>\n"
             "This is free software: you are free to change and redistribute it.\n"
             "There is NO WARRANTY, to the extent permitted by law.\n"
             "\n");
    exit (EXIT_SUCCESS);
}

static void
scan_devices_process_reply (gboolean      result,
                            const GError *error)
{
    if (!result) {
        g_printerr ("couldn't request to scan devices: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("successfully requested to scan devices\n");
}

static void
scan_devices_ready (MMManager    *manager,
                    GAsyncResult *result,
                    gpointer      nothing)
{
    gboolean operation_result;
    GError *error = NULL;

    operation_result = mm_manager_scan_devices_finish (manager,
                                                       result,
                                                       &error);
    scan_devices_process_reply (operation_result, error);

    if (cancellable) {
        g_object_unref (cancellable);
        cancellable = NULL;
    }
    if (!keep_loop)
        g_main_loop_quit (loop);
}

static void
enumerate_devices_process_reply (const GStrv   paths,
                                 const GError *error)
{
    if (error) {
        g_printerr ("couldn't enumerate devices: '%s'\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    g_print ("\n");
    if (!paths) {
        g_print ("No modems were found");
    } else {
        guint i;

        for (i = 0; paths[i]; i++) {
            g_print ("%s: '%s'\n",
                     "Found modem",
                     paths[i]);
        }
    }
    g_print ("\n");
}

static void
enumerate_devices_ready (MMManager    *manager,
                         GAsyncResult *result,
                         gpointer      nothing)
{
    GStrv paths;
    GError *error = NULL;

    paths = mm_manager_enumerate_devices_finish (manager, result, &error);
    enumerate_devices_process_reply (paths, error);
    g_strfreev (paths);

    if (cancellable) {
        g_object_unref (cancellable);
        cancellable = NULL;
    }
    if (!keep_loop)
        g_main_loop_quit (loop);
}

static void
device_added (MMManager   *manager,
              const gchar *path)
{
    g_print ("%s: '%s'\n",
             "Added modem",
             path);
    fflush (stdout);
}

static void
device_removed (MMManager   *manager,
                const gchar *path)
{
    g_print ("%s: '%s'\n",
             "Removed modem",
             path);
    fflush (stdout);
}

static void
asynchronous (MMManager *manager)
{
    g_debug ("Running asynchronous operations...");

    /* Setup global cancellable */
    cancellable = g_cancellable_new ();

    /* Request to scan modems? */
    if (scan_modems_flag) {
        mm_manager_scan_devices_async (manager,
                                       cancellable,
                                       (GAsyncReadyCallback)scan_devices_ready,
                                       NULL);
        return;
    }

    /* Request to monitor modems? */
    if (monitor_modems_flag) {
        g_signal_connect (manager,
                          "device-added",
                          G_CALLBACK (device_added),
                          NULL);
        g_signal_connect (manager,
                          "device-removed",
                          G_CALLBACK (device_removed),
                          NULL);
    }

    /* Request to list modems? */
    if (list_modems_flag) {
        mm_manager_enumerate_devices_async (manager,
                                            cancellable,
                                            (GAsyncReadyCallback)enumerate_devices_ready,
                                            NULL);
        return;
    }
}

static void
synchronous (MMManager *manager)
{
    GError *error = NULL;

    g_debug ("Running synchronous operations...");

    /* Request to set log level? */
    if (set_logging_str) {
        MMLogLevel level;

        if (g_strcmp0 (set_logging_str, "ERR") == 0)
            level = MM_LOG_LEVEL_ERROR;
        else if (g_strcmp0 (set_logging_str, "WARN") == 0)
            level = MM_LOG_LEVEL_WARNING;
        else if (g_strcmp0 (set_logging_str, "INFO") == 0)
            level = MM_LOG_LEVEL_INFO;
        else if (g_strcmp0 (set_logging_str, "DEBUG") == 0)
            level = MM_LOG_LEVEL_DEBUG;
        else {
            g_printerr ("couldn't set unknown logging level: '%s'\n",
                        set_logging_str);
            exit (EXIT_FAILURE);
        }

        if (mm_manager_set_logging (manager, level, &error)) {
            g_printerr ("couldn't set logging level: '%s'\n",
                        error ? error->message : "unknown error");
            exit (EXIT_FAILURE);
        }
        g_print ("successfully set log level '%s'\n", set_logging_str);
        return;
    }

    /* Request to scan modems? */
    if (scan_modems_flag) {
        gboolean result;

        result = mm_manager_scan_devices (manager, &error);
        scan_devices_process_reply (result, error);
        return;
    }

    /* Request to list modems? */
    if (list_modems_flag) {
        GStrv paths;

        paths = mm_manager_enumerate_devices (manager, &error);
        enumerate_devices_process_reply (paths, error);
        g_strfreev (paths);
        return;
    }
}

static void
ensure_single_action (void)
{
    guint n_actions;

    n_actions = (scan_modems_flag +
                 list_modems_flag +
                 monitor_modems_flag +
                 (set_logging_str ? 1 : 0));

    if (n_actions == 0)
        print_version_and_exit ();

    if (n_actions > 1) {
        g_printerr ("error, too many actions requested\n");
        exit (EXIT_FAILURE);
    }

    /* Additional fixes to the modem monitoring request */
    if (monitor_modems_flag) {
        /* Do not stop loop after listing initial modems */
        keep_loop = TRUE;
        /* Assume an implicit list modems request */
        list_modems_flag = TRUE;
        /* Monitoring always asynchronously */
        async_flag = TRUE;
    }

    /* Additional fixes for the log level setting request */
    if (set_logging_str) {
        /* Log level setting always synchronously */
        async_flag = FALSE;
        /* Always stop loop after setting log level */
        keep_loop = FALSE;
    }
}

gint
main (gint argc, gchar **argv)
{
    GDBusConnection *connection;
    MMManager *manager;
    GOptionContext *context;
    GError *error = NULL;

    setlocale (LC_ALL, "");

    context = g_option_context_new ("- Control and monitor the ModemManager");
    g_option_context_add_main_entries (context, entries, NULL);
    g_option_context_parse (context, &argc, &argv, NULL);

    if (version_flag)
        print_version_and_exit ();

    /* We must have exactly 1 action requested */
    ensure_single_action ();

    g_type_init ();

    /* Setup signals */
    signal (SIGINT, signals_handler);
    signal (SIGHUP, signals_handler);
    signal (SIGTERM, signals_handler);

    /* Setup dbus connection to use */
    connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!connection) {
        g_printerr ("couldn't get bus: %s\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    /* Create new manager */
    manager = mm_manager_new (connection, NULL, &error);
    if (!manager) {
        g_printerr ("couldn't create manager: %s\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    if (async_flag) {
        loop = g_main_loop_new (NULL, FALSE);
        asynchronous (manager);
        g_main_loop_run (loop);
        g_main_loop_unref (loop);
    }
    else
        synchronous (manager);

    g_object_unref (manager);
    g_object_unref (connection);
    if (cancellable)
        g_object_unref (cancellable);
    g_option_context_free (context);

    return EXIT_SUCCESS;
}

