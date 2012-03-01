/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * mmcli -- Control modem status & access information from the command line
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

#include <stdlib.h>

#include <libmm-glib.h>

#include "mmcli-common.h"

static void
manager_new_ready (GDBusConnection *connection,
                   GAsyncResult *res,
                   GSimpleAsyncResult *simple)
{
    MMManager *manager;
    gchar *name_owner;
    GError *error = NULL;

    manager = mm_manager_new_finish (res, &error);
    if (!manager) {
        g_printerr ("error: couldn't create manager: %s\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    name_owner = g_dbus_object_manager_client_get_name_owner (G_DBUS_OBJECT_MANAGER_CLIENT (manager));
    if (!name_owner) {
        g_printerr ("error: couldn't find the ModemManager process in the bus\n");
        exit (EXIT_FAILURE);
    }

    g_debug ("ModemManager process found at '%s'", name_owner);
    g_free (name_owner);



    g_simple_async_result_set_op_res_gpointer (simple, manager, NULL);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

MMManager *
mmcli_get_manager_finish (GAsyncResult *res)
{
    return g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
}

void
mmcli_get_manager (GDBusConnection *connection,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (connection),
                                        callback,
                                        user_data,
                                        mmcli_get_manager);
    mm_manager_new (connection,
                    G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                    cancellable,
                    (GAsyncReadyCallback)manager_new_ready,
                    result);
}

MMManager *
mmcli_get_manager_sync (GDBusConnection *connection)
{
    MMManager *manager;
    gchar *name_owner;
    GError *error = NULL;

    manager = mm_manager_new_sync (connection,
                                   G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                   NULL,
                                   &error);
    if (!manager) {
        g_printerr ("error: couldn't create manager: %s\n",
                    error ? error->message : "unknown error");
        exit (EXIT_FAILURE);
    }

    name_owner = g_dbus_object_manager_client_get_name_owner (G_DBUS_OBJECT_MANAGER_CLIENT (manager));
    if (!name_owner) {
        g_printerr ("error: couldn't find the ModemManager process in the bus\n");
        exit (EXIT_FAILURE);
    }

    g_debug ("ModemManager process found at '%s'", name_owner);
    g_free (name_owner);

    return manager;
}

#define MODEM_PATH_TAG "modem-path-tag"

static MMObject *
find_modem (MMManager *manager,
            const gchar *modem_path)
{
    GList *modems;
    GList *l;
    MMObject *found = NULL;

    modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
    for (l = modems; l; l = g_list_next (l)) {
        MMObject *modem = MM_OBJECT (l->data);

        if (g_str_equal (mm_object_get_path (modem), modem_path)) {
            found = g_object_ref (modem);
            break;
        }
    }
    g_list_foreach (modems, (GFunc)g_object_unref, NULL);
    g_list_free (modems);

    if (!found) {
        g_printerr ("error: couldn't find modem at '%s'\n", modem_path);
        exit (EXIT_FAILURE);
    }

    g_debug ("Modem found at '%s'\n", modem_path);

    return found;
}

static gchar *
get_modem_path (const gchar *modem_str)
{
    gchar *modem_path;

    /* We must have a given modem specified */
    if (!modem_str) {
        g_printerr ("error: no modem was specified\n");
        exit (EXIT_FAILURE);
    }

    /* Modem path may come in two ways: full DBus path or just modem index.
     * If it is a modem index, we'll need to generate the DBus path ourselves */
    if (modem_str[0] == '/')
        modem_path = g_strdup (modem_str);
    else {
        if (g_ascii_isdigit (modem_str[0]))
            modem_path = g_strdup_printf (MM_DBUS_PATH "/Modems/%s", modem_str);
        else {
            g_printerr ("error: invalid modem string specified: '%s'\n",
                        modem_str);
            exit (EXIT_FAILURE);
        }
    }

    return modem_path;
}

typedef struct {
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    gchar *modem_path;
    MMManager *manager;
    MMObject *object;
} GetModemContext;

static void
get_modem_context_free (GetModemContext *ctx)
{
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    g_free (ctx->modem_path);
    g_free (ctx);
}

static void
get_modem_context_complete (GetModemContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    ctx->result = NULL;
}

MMObject *
mmcli_get_modem_finish (GAsyncResult *res,
                        MMManager **o_manager)
{
    GetModemContext *ctx;

    ctx = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    if (o_manager)
        *o_manager = g_object_ref (ctx->manager);

    return g_object_ref (ctx->object);
}

static void
get_manager_ready (GDBusConnection *connection,
                   GAsyncResult *res,
                   GetModemContext *ctx)
{
    ctx->manager = mmcli_get_manager_finish (res);
    ctx->object = find_modem (ctx->manager, ctx->modem_path);
    g_simple_async_result_set_op_res_gpointer (
        ctx->result,
        ctx,
        (GDestroyNotify)get_modem_context_free);
    get_modem_context_complete (ctx);
}

void
mmcli_get_modem (GDBusConnection *connection,
                 const gchar *modem_str,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    GetModemContext *ctx;

    ctx = g_new0 (GetModemContext, 1);
    ctx->modem_path = get_modem_path (modem_str);
    ctx->result = g_simple_async_result_new (G_OBJECT (connection),
                                             callback,
                                             user_data,
                                             mmcli_get_modem);

    mmcli_get_manager (connection,
                       cancellable,
                       (GAsyncReadyCallback)get_manager_ready,
                       ctx);
}

MMObject *
mmcli_get_modem_sync (GDBusConnection *connection,
                      const gchar *modem_str,
                      MMManager **o_manager)
{
    MMManager *manager;
    MMObject *found;
    gchar *modem_path;

    manager = mmcli_get_manager_sync (connection);
    modem_path = get_modem_path (modem_str);
    found = find_modem (manager, modem_path);

    if (o_manager)
        *o_manager = manager;
    else
        g_object_unref (manager);
    g_free (modem_path);

    return found;
}

static MMBearer *
find_bearer_in_list (GList *list,
                     const gchar *bearer_path)
{
    GList *l;

    for (l = list; l; l = g_list_next (l)) {
        MMBearer *bearer = MM_BEARER (l->data);

        if (g_str_equal (mm_bearer_get_path (bearer), bearer_path)) {
            g_debug ("Bearer found at '%s'\n", bearer_path);
            return g_object_ref (bearer);
        }
    }

    g_printerr ("error: couldn't find bearer at '%s'\n", bearer_path);
    exit (EXIT_FAILURE);
    return NULL;
}

typedef struct {
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    gchar *bearer_path;
    MMManager *manager;
    GList *modems;
    MMObject *current;
    MMBearer *bearer;
} GetBearerContext;

static void
get_bearer_context_free (GetBearerContext *ctx)
{
    if (ctx->current)
        g_object_unref (ctx->current);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    if (ctx->manager)
        g_object_unref (ctx->manager);
    if (ctx->bearer)
        g_object_unref (ctx->bearer);
    g_list_foreach (ctx->modems, (GFunc)g_object_unref, NULL);
    g_list_free (ctx->modems);
    g_free (ctx->bearer_path);
    g_free (ctx);
}

static void
get_bearer_context_complete (GetBearerContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    ctx->result = NULL;
}

MMBearer *
mmcli_get_bearer_finish (GAsyncResult *res,
                         MMManager **o_manager,
                         MMObject **o_object)
{
    GetBearerContext *ctx;

    ctx = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    if (o_manager)
        *o_manager = g_object_ref (ctx->manager);
    if (o_object)
        *o_object = g_object_ref (ctx->current);
    return g_object_ref (ctx->bearer);
}

static void look_for_bearer_in_modem (GetBearerContext *ctx);

static void
list_bearers_ready (MMModem *modem,
                    GAsyncResult *res,
                    GetBearerContext *ctx)
{
    GList *bearers;
    GError *error = NULL;

    bearers = mm_modem_list_bearers_finish (modem, res, &error);
    if (error) {
        g_printerr ("error: couldn't list bearers at '%s': '%s'\n",
                    mm_modem_get_path (modem),
                    error->message);
        exit (EXIT_FAILURE);
    }

    ctx->bearer = find_bearer_in_list (bearers, ctx->bearer_path);
    g_list_foreach (bearers, (GFunc)g_object_unref, NULL);
    g_list_free (bearers);

    /* Found! */
    if (ctx->bearer) {
        g_simple_async_result_set_op_res_gpointer (
            ctx->result,
            ctx,
            (GDestroyNotify)get_bearer_context_free);
        get_bearer_context_complete (ctx);
        return;
    }

    /* Not found, try with next modem */
    look_for_bearer_in_modem (ctx);
}

static void
look_for_bearer_in_modem (GetBearerContext *ctx)
{
    MMModem *modem;

    if (!ctx->modems) {
        g_printerr ("error: couldn't find bearer at '%s': 'not found in any modem'\n",
                    ctx->bearer_path);
        exit (EXIT_FAILURE);
    }

    /* Loop looking for the bearer in each modem found */
    ctx->current = MM_OBJECT (ctx->modems->data);
    ctx->modems = g_list_delete_link (ctx->modems, ctx->modems);
    g_debug ("Looking for bearer '%s' in modem '%s'...",
             ctx->bearer_path,
             mm_object_get_path (ctx->current));

    modem = mm_object_get_modem (ctx->current);
    mm_modem_list_bearers (modem,
                           ctx->cancellable,
                           (GAsyncReadyCallback)list_bearers_ready,
                           ctx);
    g_object_unref (modem);
}

static void
get_bearer_manager_ready (GDBusConnection *connection,
                          GAsyncResult *res,
                          GetBearerContext *ctx)
{
    ctx->manager = mmcli_get_manager_finish (res);
    ctx->modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (ctx->manager));
    if (!ctx->modems) {
        g_printerr ("error: couldn't find bearer at '%s': 'no modems found'\n",
                    ctx->bearer_path);
        exit (EXIT_FAILURE);
    }

    look_for_bearer_in_modem (ctx);
}

void
mmcli_get_bearer (GDBusConnection *connection,
                  const gchar *bearer_path,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GetBearerContext *ctx;

    ctx = g_new0 (GetBearerContext, 1);
    ctx->bearer_path = g_strdup (bearer_path);
    if (cancellable)
        ctx->cancellable = g_object_ref (cancellable);
    ctx->result = g_simple_async_result_new (G_OBJECT (connection),
                                             callback,
                                             user_data,
                                             mmcli_get_modem);
    mmcli_get_manager (connection,
                       cancellable,
                       (GAsyncReadyCallback)get_bearer_manager_ready,
                       ctx);
}

MMBearer *
mmcli_get_bearer_sync (GDBusConnection *connection,
                       const gchar *bearer_path,
                       MMManager **o_manager,
                       MMObject **o_object)
{
    MMManager *manager;
    GList *modems;
    GList *l;
    MMBearer *found = NULL;

    manager = mmcli_get_manager_sync (connection);
    modems = g_dbus_object_manager_get_objects (G_DBUS_OBJECT_MANAGER (manager));
    if (!modems) {
        g_printerr ("error: couldn't find bearer at '%s': 'no modems found'\n",
                    bearer_path);
        exit (EXIT_FAILURE);
    }

    for (l = modems; !found && l; l = g_list_next (l)) {
        GError *error = NULL;
        MMObject *object;
        MMModem *modem;
        GList *bearers;

        object = MM_OBJECT (l->data);
        modem = mm_object_get_modem (object);
        bearers = mm_modem_list_bearers_sync (modem, NULL, &error);
        if (error) {
            g_printerr ("error: couldn't list bearers at '%s': '%s'\n",
                        mm_modem_get_path (modem),
                        error->message);
            exit (EXIT_FAILURE);
        }

        found = find_bearer_in_list (bearers, bearer_path);
        g_list_foreach (bearers, (GFunc)g_object_unref, NULL);
        g_list_free (bearers);

        if (o_object)
            *o_object = g_object_ref (object);

        g_object_unref (modem);
    }

    g_list_foreach (modems, (GFunc)g_object_unref, NULL);
    g_list_free (modems);

    if (o_manager)
        *o_manager = manager;
    else
        g_object_unref (manager);

    return found;
}

const gchar *
mmcli_get_bearer_ip_method_string (MMBearerIpMethod method)
{
    static GEnumClass *enum_class = NULL;
    GEnumValue *value;

    if (!enum_class)
        enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_BEARER_IP_METHOD));

    value = g_enum_get_value (enum_class, method);
    return value->value_nick;
}

const gchar *
mmcli_get_state_string (MMModemState state)
{
    static GEnumClass *enum_class = NULL;
    GEnumValue *value;

    if (!enum_class)
        enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_MODEM_STATE));

    value = g_enum_get_value (enum_class, state);
    return value->value_nick;
}

const gchar *
mmcli_get_state_reason_string (MMModemStateChangeReason reason)
{
    switch (reason) {
    case MM_MODEM_STATE_CHANGE_REASON_UNKNOWN:
        return "None or unknown";
    case MM_MODEM_STATE_CHANGE_REASON_USER_REQUESTED:
        return "User request";
    case MM_MODEM_STATE_CHANGE_REASON_SUSPEND:
        return "Suspend";
    }

    g_warn_if_reached ();
    return NULL;
}

const gchar *
mmcli_get_lock_string (MMModemLock lock)
{
    static GEnumClass *enum_class = NULL;
    GEnumValue *value;

    if (!enum_class)
        enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_MODEM_LOCK));

    value = g_enum_get_value (enum_class, lock);
    return value->value_nick;
}

const gchar *
mmcli_get_3gpp_network_availability_string (MMModem3gppNetworkAvailability availability)
{
    static GEnumClass *enum_class = NULL;
    GEnumValue *value;

    if (!enum_class)
        enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_MODEM_3GPP_NETWORK_AVAILABILITY));

    value = g_enum_get_value (enum_class, availability);
    return value->value_nick;
}

const gchar *
mmcli_get_3gpp_registration_state_string (MMModem3gppRegistrationState state)
{
    static GEnumClass *enum_class = NULL;
    GEnumValue *value;

    if (!enum_class)
        enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_MODEM_3GPP_REGISTRATION_STATE));

    value = g_enum_get_value (enum_class, state);
    return value->value_nick;
}

/* Common options */
static gchar *modem_str;
static gchar *bearer_str;

static GOptionEntry entries[] = {
    { "modem", 'm', 0, G_OPTION_ARG_STRING, &modem_str,
      "Specify modem by path or index. Shows modem information if no action specified.",
      "[PATH|INDEX]"
    },
    { "bearer", 'b', 0, G_OPTION_ARG_STRING, &bearer_str,
      "Specify bearer by path. Shows bearer information if no action specified.",
      "[PATH]"
    },
    { NULL }
};

GOptionGroup *
mmcli_get_common_option_group (void)
{
	GOptionGroup *group;

	/* Status options */
	group = g_option_group_new ("common",
	                            "Common options",
	                            "Show common options",
	                            NULL,
	                            NULL);
	g_option_group_add_entries (group, entries);

	return group;
}

const gchar *
mmcli_get_common_modem_string (void)
{
    return modem_str;
}

const gchar *
mmcli_get_common_bearer_string (void)
{
    return bearer_str;
}
