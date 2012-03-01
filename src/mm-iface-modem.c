/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details:
 *
 * Copyright (C) 2011 Google, Inc.
 */


#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-modem-helpers.h"
#include "mm-iface-modem.h"
#include "mm-base-modem.h"
#include "mm-sim.h"
#include "mm-bearer-list.h"
#include "mm-log.h"

#define INDICATORS_CHECKED_TAG           "indicators-checked-tag"
#define UNSOLICITED_EVENTS_SUPPORTED_TAG "unsolicited-events-supported-tag"
static GQuark indicators_checked;
static GQuark unsolicited_events_supported;

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (InitializationContext *ctx);

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (EnablingContext *ctx);

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (DisablingContext *ctx);

/*****************************************************************************/

void
mm_iface_modem_bind_simple_status (MMIfaceModem *self,
                                   MMCommonSimpleProperties *status)
{
    MmGdbusModem *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);

    g_object_bind_property (skeleton, "state",
                            status, MM_COMMON_SIMPLE_PROPERTY_STATE,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "signal-quality",
                            status, MM_COMMON_SIMPLE_PROPERTY_SIGNAL_QUALITY,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "allowed-bands",
                            status, MM_COMMON_SIMPLE_PROPERTY_BANDS,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "access-technologies",
                            status, MM_COMMON_SIMPLE_PROPERTY_ACCESS_TECHNOLOGIES,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_unref (skeleton);
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem *self;
} DbusCallContext;

static void
dbus_call_context_free (DbusCallContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static DbusCallContext *
dbus_call_context_new (MmGdbusModem *skeleton,
                       GDBusMethodInvocation *invocation,
                       MMIfaceModem *self)
{
    DbusCallContext *ctx;

    ctx = g_new (DbusCallContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    return ctx;
}

/*****************************************************************************/

typedef struct {
    MMBearer *self;
    guint others_connected;
} CountOthersConnectedContext;

static void
bearer_list_count_others_connected (MMBearer *bearer,
                                    CountOthersConnectedContext *ctx)
{
    /* We can safely compare pointers here */
    if (bearer != ctx->self &&
        mm_bearer_get_status (bearer) == MM_BEARER_STATUS_CONNECTED) {
        ctx->others_connected++;
    }
}

static void
bearer_status_changed (MMBearer *bearer,
                       GParamSpec *pspec,
                       MMIfaceModem *self)
{
    CountOthersConnectedContext ctx;
    MMModemState new_state;
    MMBearerList *list = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &list,
                  NULL);

    ctx.self = bearer;
    ctx.others_connected = 0;

    /* We now count how many *other* bearers are connected */
    mm_bearer_list_foreach (list,
                            (MMBearerListForeachFunc)bearer_list_count_others_connected,
                            &ctx);

    /* If no other bearers are connected, change modem state */
    if (!ctx.others_connected) {
        switch (mm_bearer_get_status (bearer)) {
        case MM_BEARER_STATUS_CONNECTED:
            new_state = MM_MODEM_STATE_CONNECTED;
            break;
        case MM_BEARER_STATUS_CONNECTING:
            new_state = MM_MODEM_STATE_CONNECTING;
            break;
        case MM_BEARER_STATUS_DISCONNECTING:
            new_state = MM_MODEM_STATE_DISCONNECTING;
            break;
        case MM_BEARER_STATUS_DISCONNECTED:
            new_state = MM_MODEM_STATE_REGISTERED;
            break;
        }

        mm_iface_modem_update_state (self,
                                     new_state,
                                     MM_MODEM_STATE_CHANGE_REASON_USER_REQUESTED);
    }
}

MMBearer *
mm_iface_modem_create_bearer_finish (MMIfaceModem *self,
                                     GAsyncResult *res,
                                     GError **error)
{
    MMBearer *bearer;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    bearer = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));

    return g_object_ref (bearer);
}

static void
create_bearer_ready (MMIfaceModem *self,
                     GAsyncResult *res,
                     GSimpleAsyncResult *simple)
{
    MMBearer *bearer;
    GError *error = NULL;

    bearer = MM_IFACE_MODEM_GET_INTERFACE (self)->create_bearer_finish (self,
                                                                        res,
                                                                        &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else {
        MMBearerList *list = NULL;

        g_object_get (self,
                      MM_IFACE_MODEM_BEARER_LIST, &list,
                      NULL);

        if (!mm_bearer_list_add_bearer (list, bearer, &error))
            g_simple_async_result_take_error (simple, error);
        else {
            /* If bearer properly created and added to the list, follow its
             * status */
            g_signal_connect (bearer,
                              "notify::"  MM_BEARER_STATUS,
                              (GCallback)bearer_status_changed,
                              self);
            g_simple_async_result_set_op_res_gpointer (simple,
                                                       g_object_ref (bearer),
                                                       g_object_unref);
        }
        g_object_unref (bearer);
        g_object_unref (list);
    }

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_iface_modem_create_bearer (MMIfaceModem *self,
                              gboolean force,
                              MMCommonBearerProperties *properties,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    MMBearerList *list = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &list,
                  NULL);

    if (mm_bearer_list_get_count (list) == mm_bearer_list_get_max (list)) {
        if (!force) {
            g_simple_async_report_error_in_idle (
                G_OBJECT (self),
                callback,
                user_data,
                MM_CORE_ERROR,
                MM_CORE_ERROR_TOO_MANY,
                "Cannot add new bearer: already reached maximum (%u)",
                mm_bearer_list_get_count (list));
            g_object_unref (list);
            return;
        }

        /* We are told to force the creation of the new bearer.
         * We'll remove all existing bearers, and then go on creating the new one */
        mm_bearer_list_delete_all_bearers (list);
    }

    MM_IFACE_MODEM_GET_INTERFACE (self)->create_bearer (
        self,
        properties,
        (GAsyncReadyCallback)create_bearer_ready,
        g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   mm_iface_modem_create_bearer));
    g_object_unref (list);
}

static void
handle_create_bearer_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            DbusCallContext *ctx)
{
    MMBearer *bearer;
    GError *error = NULL;

    bearer = mm_iface_modem_create_bearer_finish (self, res, &error);
    if (!bearer)
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else {
        mm_gdbus_modem_complete_create_bearer (ctx->skeleton,
                                               ctx->invocation,
                                               mm_bearer_get_path (bearer));
        g_object_unref (bearer);
    }
    dbus_call_context_free (ctx);
}

static gboolean
handle_create_bearer (MmGdbusModem *skeleton,
                      GDBusMethodInvocation *invocation,
                      GVariant *dictionary,
                      MMIfaceModem *self)
{
    GError *error = NULL;
    MMCommonBearerProperties *properties;

    properties = mm_common_bearer_properties_new_from_dictionary (dictionary, &error);
    if (!properties) {
        g_dbus_method_invocation_take_error (invocation, error);
    } else {
        mm_iface_modem_create_bearer (
            self,
            FALSE, /* don't force when request comes from DBus */
            properties,
            (GAsyncReadyCallback)handle_create_bearer_ready,
            dbus_call_context_new (skeleton,
                                   invocation,
                                   self));
        g_object_unref (properties);
    }

    return TRUE;
}

/*****************************************************************************/

static gboolean
handle_delete_bearer (MmGdbusModem *skeleton,
                      GDBusMethodInvocation *invocation,
                      const gchar *arg_bearer,
                      MMIfaceModem *self)
{
    MMBearerList *list = NULL;
    GError *error = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &list,
                  NULL);

    if (!mm_bearer_list_delete_bearer (list, arg_bearer, &error))
        g_dbus_method_invocation_take_error (invocation, error);
    else
        mm_gdbus_modem_complete_delete_bearer (skeleton, invocation);

    g_object_unref (list);
    return TRUE;
}

/*****************************************************************************/

static gboolean
handle_list_bearers (MmGdbusModem *skeleton,
                     GDBusMethodInvocation *invocation,
                     MMIfaceModem *self)
{
    GStrv paths;
    MMBearerList *list = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &list,
                  NULL);

    paths = mm_bearer_list_get_paths (list);
    mm_gdbus_modem_complete_list_bearers (skeleton,
                                          invocation,
                                          (const gchar *const *)paths);

    g_strfreev (paths);
    g_object_unref (list);
    return TRUE;
}

/*****************************************************************************/

void
mm_iface_modem_update_access_tech (MMIfaceModem *self,
                                   MMModemAccessTechnology new_access_tech,
                                   guint32 mask)
{
    MmGdbusModem *skeleton = NULL;
    MMModemAccessTechnology old_access_tech;
    MMModemAccessTechnology built_access_tech;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);

    old_access_tech = mm_gdbus_modem_get_access_technologies (skeleton);

    /* Build the new access tech */
    built_access_tech = old_access_tech;
    built_access_tech &= ~mask;
    built_access_tech |= new_access_tech;

    if (built_access_tech != old_access_tech) {
        gchar *old_access_tech_string;
        gchar *new_access_tech_string;
        const gchar *dbus_path;

        mm_gdbus_modem_set_access_technologies (skeleton, built_access_tech);

        /* Log */
        old_access_tech_string = mm_common_get_access_technologies_string (old_access_tech);
        new_access_tech_string = mm_common_get_access_technologies_string (built_access_tech);
        dbus_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (self));
        mm_info ("Modem %s: access technology changed (%s -> %s)",
                 dbus_path,
                 old_access_tech_string,
                 new_access_tech_string);
        g_free (old_access_tech_string);
        g_free (new_access_tech_string);
    }

    g_object_unref (skeleton);
}

/*****************************************************************************/

#define SIGNAL_QUALITY_RECENT_TIMEOUT_SEC 60
#define SIGNAL_QUALITY_RECENT_TAG "signal-quality-recent-tag"
static GQuark signal_quality_recent;

static gboolean
expire_signal_quality (MMIfaceModem *self)
{
    GVariant *old;
    guint signal_quality = 0;
    gboolean recent = FALSE;
    MmGdbusModem *skeleton = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);

    old = mm_gdbus_modem_get_signal_quality (skeleton);
    g_variant_get (old,
                   "(ub)",
                   &signal_quality,
                   &recent);

    /* If value is already not recent, we're done */
    if (recent) {
        mm_dbg ("Signal quality value not updated in %us, "
                "marking as not being recent",
                SIGNAL_QUALITY_RECENT_TIMEOUT_SEC);
        mm_gdbus_modem_set_signal_quality (skeleton,
                                           g_variant_new ("(ub)",
                                                          signal_quality,
                                                          FALSE));
    }

    g_object_set_qdata (G_OBJECT (self),
                        signal_quality_recent,
                        GUINT_TO_POINTER (0));
    return FALSE;
}

static void
update_signal_quality (MMIfaceModem *self,
                       guint signal_quality,
                       gboolean expire)
{
    guint timeout_source;
    MmGdbusModem *skeleton = NULL;
    const gchar *dbus_path;

    if (G_UNLIKELY (!signal_quality_recent))
        signal_quality_recent = (g_quark_from_static_string (
                                     SIGNAL_QUALITY_RECENT_TAG));

    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);

    /* Note: we always set the new value, even if the signal quality level
     * is the same, in order to provide an up to date 'recent' flag.
     * The only exception being if 'expire' is FALSE; in that case we assume
     * the value won't expire and therefore can be considered obsolete
     * already. */
    mm_gdbus_modem_set_signal_quality (skeleton,
                                       g_variant_new ("(ub)",
                                                      signal_quality,
                                                      expire));

    dbus_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (self));
    mm_info ("Modem %s: signal quality updated (%u)",
             dbus_path,
             signal_quality);

    /* Setup timeout to clear the 'recent' flag */
    timeout_source = GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (self),
                                                           signal_quality_recent));
    /* Remove any previous expiration refresh timeout */
    if (timeout_source)
        g_source_remove (timeout_source);

    /* If we got a new expirable value, setup new timeout */
    if (expire) {
        timeout_source = g_timeout_add_seconds (SIGNAL_QUALITY_RECENT_TIMEOUT_SEC,
                                                (GSourceFunc)expire_signal_quality,
                                                self);
        g_object_set_qdata (G_OBJECT (self),
                            signal_quality_recent,
                            GUINT_TO_POINTER (timeout_source));
    }

    g_object_unref (skeleton);
}

void
mm_iface_modem_update_signal_quality (MMIfaceModem *self,
                                      guint signal_quality)
{
    update_signal_quality (self, signal_quality, TRUE);
}

/*****************************************************************************/

#define SIGNAL_QUALITY_CHECK_TIMEOUT_SEC 30
#define PERIODIC_SIGNAL_QUALITY_CHECK_ENABLED_TAG "signal-quality-check-timeout-enabled-tag"
#define PERIODIC_SIGNAL_QUALITY_CHECK_RUNNING_TAG "signal-quality-check-timeout-running-tag"
static GQuark signal_quality_check_enabled;
static GQuark signal_quality_check_running;

static void
signal_quality_check_ready (MMIfaceModem *self,
                            GAsyncResult *res)
{
    GError *error = NULL;
    guint signal_quality;

    signal_quality = MM_IFACE_MODEM_GET_INTERFACE (self)->load_signal_quality_finish (self,
                                                                                      res,
                                                                                      &error);
    if (error) {
        mm_dbg ("Couldn't refresh signal quality: '%s'", error->message);
        g_error_free (error);
    } else
        update_signal_quality (self, signal_quality, TRUE);

    /* Remove the running tag */
    g_object_set_qdata (G_OBJECT (self),
                        signal_quality_check_running,
                        GUINT_TO_POINTER (FALSE));
}

static gboolean
periodic_signal_quality_check (MMIfaceModem *self)
{
    /* Only launch a new one if not one running already */
    if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (self),
                                               signal_quality_check_running))) {
        g_object_set_qdata (G_OBJECT (self),
                            signal_quality_check_running,
                            GUINT_TO_POINTER (TRUE));

        MM_IFACE_MODEM_GET_INTERFACE (self)->load_signal_quality (
            self,
            (GAsyncReadyCallback)signal_quality_check_ready,
            NULL);
    }

    return TRUE;
}

static void
periodic_signal_quality_check_disable (MMIfaceModem *self)
{
    guint timeout_source;

    timeout_source = GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (self),
                                                           signal_quality_check_enabled));
    if (timeout_source) {
        g_source_remove (timeout_source);
        g_object_set_qdata (G_OBJECT (self),
                            signal_quality_check_enabled,
                            GUINT_TO_POINTER (FALSE));

        /* Clear the current value */
        update_signal_quality (self, 0, FALSE);

        mm_dbg ("Periodic signal quality checks disabled");
    }
}

static void
periodic_signal_quality_check_enable (MMIfaceModem *self)
{
    guint timeout_source;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->load_signal_quality ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->load_signal_quality_finish) {
        /* If loading signal quality not supported, don't even bother setting up
         * a timeout */
        return;
    }

    if (G_UNLIKELY (!signal_quality_check_enabled))
        signal_quality_check_enabled = (g_quark_from_static_string (
                                            PERIODIC_SIGNAL_QUALITY_CHECK_ENABLED_TAG));
    if (G_UNLIKELY (!signal_quality_check_running))
        signal_quality_check_running = (g_quark_from_static_string (
                                            PERIODIC_SIGNAL_QUALITY_CHECK_RUNNING_TAG));

    timeout_source = GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (self),
                                                           signal_quality_check_enabled));
    if (!timeout_source) {
        mm_dbg ("Periodic signal quality checks enabled");
        timeout_source = g_timeout_add_seconds (SIGNAL_QUALITY_CHECK_TIMEOUT_SEC,
                                                (GSourceFunc)periodic_signal_quality_check,
                                                self);
        g_object_set_qdata (G_OBJECT (self),
                            signal_quality_check_enabled,
                            GUINT_TO_POINTER (timeout_source));

        /* Get first signal quality value */
        periodic_signal_quality_check (self);
    }
}

/*****************************************************************************/

static void
bearer_list_count_connected (MMBearer *bearer,
                             guint *count)
{
    if (mm_bearer_get_status (bearer) == MM_BEARER_STATUS_CONNECTED)
        (*count)++;
}

void
mm_iface_modem_update_state (MMIfaceModem *self,
                             MMModemState new_state,
                             MMModemStateReason reason)
{
    MMModemState old_state = MM_MODEM_STATE_UNKNOWN;
    MmGdbusModem *skeleton = NULL;
    MMBearerList *bearer_list = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &old_state,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  MM_IFACE_MODEM_BEARER_LIST, &bearer_list,
                  NULL);

    /* While connected we don't want registration status changes to change
     * the modem's state away from CONNECTED. */
    if ((new_state == MM_MODEM_STATE_SEARCHING ||
         new_state == MM_MODEM_STATE_REGISTERED) &&
        bearer_list &&
        old_state > MM_MODEM_STATE_REGISTERED) {
        guint connected = 0;

        mm_bearer_list_foreach (bearer_list,
                                (MMBearerListForeachFunc)bearer_list_count_connected,
                                &connected);
        if (connected > 0)
            /* Don't update state */
            new_state = old_state;
    }

    /* Update state only if different */
    if (new_state != old_state) {
        GEnumClass *enum_class;
        GEnumValue *new_value;
        GEnumValue *old_value;
        const gchar *dbus_path;

        enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_MODEM_STATE));
        new_value = g_enum_get_value (enum_class, new_state);
        old_value = g_enum_get_value (enum_class, old_state);
        dbus_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (self));
        if (dbus_path)
            mm_info ("Modem %s: state changed (%s -> %s)",
                     dbus_path,
                     old_value->value_nick,
                     new_value->value_nick);
        else
            mm_info ("Modem: state changed (%s -> %s)",
                     old_value->value_nick,
                     new_value->value_nick);
        g_type_class_unref (enum_class);

        /* The property in the interface is bound to the property
         * in the skeleton, so just updating here is enough */
        g_object_set (self,
                      MM_IFACE_MODEM_STATE, new_state,
                      NULL);

        /* Signal status change */
        mm_gdbus_modem_emit_state_changed (skeleton,
                                           old_state,
                                           new_state,
                                           reason);

        /* If we go to registered state (from unregistered), setup signal
         * quality retrieval */
        if (new_state == MM_MODEM_STATE_REGISTERED &&
            old_state < MM_MODEM_STATE_REGISTERED) {
            periodic_signal_quality_check_enable (self);
        }
        /* If we go from a registered/connected state to unregistered,
         * cleanup signal quality retrieval */
        else if (old_state >= MM_MODEM_STATE_REGISTERED &&
                 new_state < MM_MODEM_STATE_REGISTERED) {
            periodic_signal_quality_check_disable (self);
        }
    }

    if (skeleton)
        g_object_unref (skeleton);
    if (bearer_list)
        g_object_unref (bearer_list);
}

/*****************************************************************************/

static void
enable_disable_ready (MMIfaceModem *self,
                      GAsyncResult *res,
                      DbusCallContext *ctx)
{
    GError *error = NULL;

    if (!MM_BASE_MODEM_GET_CLASS (self)->enable_finish (MM_BASE_MODEM (self),
                                                        res,
                                                        &error))
        g_dbus_method_invocation_take_error (ctx->invocation,
                                             error);
    else
        mm_gdbus_modem_complete_enable (ctx->skeleton,
                                        ctx->invocation);
    dbus_call_context_free (ctx);
}

static gboolean
handle_enable (MmGdbusModem *skeleton,
               GDBusMethodInvocation *invocation,
               gboolean arg_enable,
               MMIfaceModem *self)
{
    MMModemState modem_state;

    g_assert (MM_BASE_MODEM_GET_CLASS (self)->enable != NULL);
    g_assert (MM_BASE_MODEM_GET_CLASS (self)->enable_finish != NULL);

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    if (arg_enable)
        MM_BASE_MODEM_GET_CLASS (self)->enable (MM_BASE_MODEM (self),
                                                NULL, /* cancellable */
                                                (GAsyncReadyCallback)enable_disable_ready,
                                                dbus_call_context_new (skeleton,
                                                                       invocation,
                                                                       self));
    else
        MM_BASE_MODEM_GET_CLASS (self)->disable (MM_BASE_MODEM (self),
                                                 NULL, /* cancellable */
                                                 (GAsyncReadyCallback)enable_disable_ready,
                                                 dbus_call_context_new (skeleton,
                                                                        invocation,
                                                                        self));
    return TRUE;
}

/*****************************************************************************/

static void
reset_ready (MMIfaceModem *self,
             GAsyncResult *res,
             DbusCallContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->reset_finish (self,
                                                            res,
                                                            &error))
        g_dbus_method_invocation_take_error (ctx->invocation,
                                             error);
    else
        mm_gdbus_modem_complete_reset (ctx->skeleton,
                                       ctx->invocation);
    dbus_call_context_free (ctx);
}

static gboolean
handle_reset (MmGdbusModem *skeleton,
              GDBusMethodInvocation *invocation,
              MMIfaceModem *self)
{
    MMModemState modem_state;

    /* If reseting is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->reset ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->reset_finish) {
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot reset the modem: operation not supported");
        return TRUE;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_LOCKED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot reset modem: not initialized/unlocked yet");
        break;

    case MM_MODEM_STATE_DISABLED:
    case MM_MODEM_STATE_DISABLING:
    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        MM_IFACE_MODEM_GET_INTERFACE (self)->reset (self,
                                                    (GAsyncReadyCallback)reset_ready,
                                                    dbus_call_context_new (skeleton,
                                                                           invocation,
                                                                           self));
        break;
    }

    return TRUE;
}

/*****************************************************************************/

static void
factory_reset_ready (MMIfaceModem *self,
                     GAsyncResult *res,
                     DbusCallContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->factory_reset_finish (self,
                                                                    res,
                                                                    &error))
        g_dbus_method_invocation_take_error (ctx->invocation,
                                             error);
    else
        mm_gdbus_modem_complete_factory_reset (ctx->skeleton,
                                               ctx->invocation);
    dbus_call_context_free (ctx);
}

static gboolean
handle_factory_reset (MmGdbusModem *skeleton,
                      GDBusMethodInvocation *invocation,
                      const gchar *arg_code,
                      MMIfaceModem *self)
{
    MMModemState modem_state;

    /* If reseting is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->factory_reset ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->factory_reset_finish) {
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot reset the modem to factory defaults: "
                                               "operation not supported");
        return TRUE;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_LOCKED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot reset the modem to factory defaults: "
                                               "not initialized/unlocked yet");
        break;

    case MM_MODEM_STATE_DISABLED:
    case MM_MODEM_STATE_DISABLING:
    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        MM_IFACE_MODEM_GET_INTERFACE (self)->factory_reset (self,
                                                            arg_code,
                                                            (GAsyncReadyCallback)factory_reset_ready,
                                                            dbus_call_context_new (skeleton,
                                                                                   invocation,
                                                                                   self));
        break;
    }

    return TRUE;
}

/*****************************************************************************/

gboolean
mm_iface_modem_set_allowed_bands_finish (MMIfaceModem *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
set_allowed_bands_ready (MMIfaceModem *self,
                         GAsyncResult *res,
                         GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_bands_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static gboolean
validate_allowed_bands (GArray *bands_array,
                        GError **error)
{
    /* When the array has more than one element, there MUST NOT include ANY or
     * UNKNOWN */
    if (bands_array->len > 1) {
        guint i;

        for (i = 0; i < bands_array->len; i++) {
            MMModemBand band;

            band = g_array_index (bands_array, MMModemBand, i);
            if (band == MM_MODEM_BAND_UNKNOWN ||
                band == MM_MODEM_BAND_ANY) {
                 GEnumClass *enum_class;
                 GEnumValue *value;

                 enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_MODEM_BAND));
                 value = g_enum_get_value (enum_class, band);
                 g_set_error (error,
                              MM_CORE_ERROR,
                              MM_CORE_ERROR_INVALID_ARGS,
                              "Wrong list of bands: "
                              "'%s' should have been the only element in the list",
                              value->value_nick);
                 g_type_class_unref (enum_class);
                 return FALSE;
            }
        }
    }
    return TRUE;
}

void
mm_iface_modem_set_allowed_bands (MMIfaceModem *self,
                                  GArray *bands_array,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    GSimpleAsyncResult *result;
    GError *error = NULL;

    /* If setting allowed bands is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_bands ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_bands_finish) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "Setting allowed bands not supported");
        return;
    }

    /* Validate input list of bands */
    if (!validate_allowed_bands (bands_array, &error)) {
        g_simple_async_report_take_gerror_in_idle (G_OBJECT (self),
                                                   callback,
                                                   user_data,
                                                   error);
        return;
    }

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_iface_modem_set_allowed_bands);
    MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_bands (self,
                                                            bands_array,
                                                            (GAsyncReadyCallback)set_allowed_bands_ready,
                                                            result);
}

static void
handle_set_allowed_bands_ready (MMIfaceModem *self,
                                GAsyncResult *res,
                                DbusCallContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_bands_finish (self,
                                                                        res,
                                                                        &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_complete_set_allowed_bands (ctx->skeleton,
                                                   ctx->invocation);
    dbus_call_context_free (ctx);
}

static gboolean
handle_set_allowed_bands (MmGdbusModem *skeleton,
                          GDBusMethodInvocation *invocation,
                          GVariant *bands_variant,
                          MMIfaceModem *self)
{
    MMModemState modem_state = MM_MODEM_STATE_UNKNOWN;

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_LOCKED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot set allowed bands: "
                                               "not initialized/unlocked yet");
        break;

    case MM_MODEM_STATE_DISABLED:
    case MM_MODEM_STATE_DISABLING:
    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED: {
        GArray *bands_array;

        bands_array = mm_common_bands_variant_to_garray (bands_variant);
        mm_iface_modem_set_allowed_bands (self,
                                          bands_array,
                                          (GAsyncReadyCallback)handle_set_allowed_bands_ready,
                                          dbus_call_context_new (skeleton,
                                                                 invocation,
                                                                 self));
        g_array_unref (bands_array);
        break;
      }
    }

    return TRUE;
}

/*****************************************************************************/
/* ALLOWED MODES */

gboolean
mm_iface_modem_set_allowed_modes_finish (MMIfaceModem *self,
                                         GAsyncResult *res,
                                         GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
set_allowed_modes_ready (MMIfaceModem *self,
                         GAsyncResult *res,
                         GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_modes_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_iface_modem_set_allowed_modes (MMIfaceModem *self,
                                  MMModemMode allowed,
                                  MMModemMode preferred,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    GSimpleAsyncResult *result;

    /* If setting allowed modes is not implemented, report an error */
    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_modes ||
        !MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_modes_finish) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_UNSUPPORTED,
                                             "Setting allowed modes not supported");
        return;
    }

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_iface_modem_set_allowed_modes);
    MM_IFACE_MODEM_GET_INTERFACE (self)->set_allowed_modes (self,
                                                            allowed,
                                                            preferred,
                                                            (GAsyncReadyCallback)set_allowed_modes_ready,
                                                            result);
}

static void
handle_set_allowed_modes_ready (MMIfaceModem *self,
                                GAsyncResult *res,
                                DbusCallContext *ctx)
{
    GError *error = NULL;

    if (mm_iface_modem_set_allowed_modes_finish (self, res, &error))
        g_dbus_method_invocation_take_error (ctx->invocation, error);
    else
        mm_gdbus_modem_complete_set_allowed_modes (ctx->skeleton,
                                                   ctx->invocation);
    dbus_call_context_free (ctx);
}

static gboolean
handle_set_allowed_modes (MmGdbusModem *skeleton,
                          GDBusMethodInvocation *invocation,
                          guint modes,
                          guint preferred,
                          MMIfaceModem *self)
{
    MMModemState modem_state = MM_MODEM_STATE_UNKNOWN;

    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_UNKNOWN:
    case MM_MODEM_STATE_LOCKED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot set allowed modes: "
                                               "not initialized/unlocked yet");
        break;

    case MM_MODEM_STATE_DISABLED:
    case MM_MODEM_STATE_DISABLING:
    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        mm_iface_modem_set_allowed_modes (self,
                                          modes,
                                          preferred,
                                          (GAsyncReadyCallback)handle_set_allowed_modes_ready,
                                          dbus_call_context_new (skeleton,
                                                                 invocation,
                                                                 self));
        break;
    }

    return TRUE;
}

/*****************************************************************************/

typedef struct _UnlockCheckContext UnlockCheckContext;
struct _UnlockCheckContext {
    MMIfaceModem *self;
    MMAtSerialPort *port;
    guint pin_check_tries;
    guint pin_check_timeout_id;
    GSimpleAsyncResult *result;
    MmGdbusModem *skeleton;
};

static UnlockCheckContext *
unlock_check_context_new (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    UnlockCheckContext *ctx;

    ctx = g_new0 (UnlockCheckContext, 1);
    ctx->self = g_object_ref (self);
    ctx->port = g_object_ref (mm_base_modem_get_port_primary (MM_BASE_MODEM (self)));
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             unlock_check_context_new);
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);
    return ctx;
}

static void
unlock_check_context_free (UnlockCheckContext *ctx)
{
    g_object_unref (ctx->self);
    g_object_unref (ctx->port);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static gboolean
restart_initialize_idle (MMIfaceModem *self)
{
    mm_iface_modem_initialize (self,
                               mm_base_modem_get_port_primary (MM_BASE_MODEM (self)),
                               NULL,
                               NULL);
    return FALSE;
}

static void
set_lock_status (MMIfaceModem *self,
                 MmGdbusModem *skeleton,
                 MMModemLock lock)
{
    MMModemLock old_lock;

    old_lock = mm_gdbus_modem_get_unlock_required (skeleton);
    mm_gdbus_modem_set_unlock_required (skeleton, lock);

    /* We don't care about SIM-PIN2/SIM-PUK2 since the device is
     * operational without it. */
    if (lock == MM_MODEM_LOCK_NONE ||
        lock == MM_MODEM_LOCK_SIM_PIN2 ||
        lock == MM_MODEM_LOCK_SIM_PUK2) {
        if (old_lock != MM_MODEM_LOCK_NONE) {
            /* Notify transition from UNKNOWN/LOCKED to DISABLED */
            mm_iface_modem_update_state (self,
                                         MM_MODEM_STATE_DISABLED,
                                         MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);
            /* Only restart initialization if going from LOCKED to DISABLED */
            if (old_lock != MM_MODEM_LOCK_UNKNOWN)
                g_idle_add ((GSourceFunc)restart_initialize_idle, self);
        }
    } else {
        if (old_lock == MM_MODEM_LOCK_UNKNOWN) {
            /* Notify transition from UNKNOWN to LOCKED */
            mm_iface_modem_update_state (self,
                                         MM_MODEM_STATE_LOCKED,
                                         MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);
        }
    }
}

MMModemLock
mm_iface_modem_unlock_check_finish (MMIfaceModem *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error)) {
        return MM_MODEM_LOCK_UNKNOWN;
    }

    return (MMModemLock) GPOINTER_TO_UINT (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
}

static void unlock_check_ready (MMIfaceModem *self,
                                GAsyncResult *res,
                                UnlockCheckContext *ctx);

static gboolean
unlock_check_again  (UnlockCheckContext *ctx)
{
    ctx->pin_check_timeout_id = 0;

    MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_required (
        ctx->self,
        (GAsyncReadyCallback)unlock_check_ready,
        ctx);
    return FALSE;
}

static void
unlock_check_ready (MMIfaceModem *self,
                    GAsyncResult *res,
                    UnlockCheckContext *ctx)
{
    GError *error = NULL;
    MMModemLock lock;

    lock = MM_IFACE_MODEM_GET_INTERFACE (self)->load_unlock_required_finish (self,
                                                                             res,
                                                                             &error);
    if (error) {
        /* Treat several SIM related errors as critical and abort the checks
         * TODO: do this only after the retries? */
        if (g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_NOT_INSERTED) ||
            g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_FAILURE) ||
            g_error_matches (error,
                             MM_MOBILE_EQUIPMENT_ERROR,
                             MM_MOBILE_EQUIPMENT_ERROR_SIM_WRONG)) {
            g_simple_async_result_take_error (ctx->result, error);
            g_simple_async_result_complete (ctx->result);
            unlock_check_context_free (ctx);
            return;
        }

        /* Retry up to 3 times */
        if (mm_gdbus_modem_get_unlock_required (ctx->skeleton) != MM_MODEM_LOCK_NONE &&
            ++ctx->pin_check_tries < 3) {

            if (ctx->pin_check_timeout_id)
                g_source_remove (ctx->pin_check_timeout_id);
            ctx->pin_check_timeout_id = g_timeout_add_seconds (
                2,
                (GSourceFunc)unlock_check_again,
                ctx);
            return;
        }

        /* If reached max retries and still reporting error, set UNKNOWN */
        lock = MM_MODEM_LOCK_UNKNOWN;
    }

    /* Update lock status and modem status if needed */
    set_lock_status (self, ctx->skeleton, lock);

    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               GUINT_TO_POINTER (lock),
                                               NULL);
    g_simple_async_result_complete (ctx->result);
    unlock_check_context_free (ctx);
}

void
mm_iface_modem_unlock_check (MMIfaceModem *self,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    UnlockCheckContext *ctx;

    ctx = unlock_check_context_new (self, callback, user_data);

    if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_required &&
        MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_required_finish) {
        MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_required (
            self,
            (GAsyncReadyCallback)unlock_check_ready,
            ctx);
        return;
    }

    /* Just assume that no lock is required */
    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               GUINT_TO_POINTER (MM_MODEM_LOCK_NONE),
                                               NULL);
    g_simple_async_result_complete_in_idle (ctx->result);
    unlock_check_context_free (ctx);
}

/*****************************************************************************/

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS,
    DISABLING_STEP_MODEM_POWER_DOWN,
    DISABLING_STEP_CLOSE_PORT,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    MMIfaceModem *self;
    MMAtSerialPort *primary;
    DisablingStep step;
    MMModemState previous_state;
    gboolean disabled;
    GSimpleAsyncResult *result;
    MmGdbusModem *skeleton;
};

static DisablingContext *
disabling_context_new (MMIfaceModem *self,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    DisablingContext *ctx;

    ctx = g_new0 (DisablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->primary = g_object_ref (mm_base_modem_get_port_primary (MM_BASE_MODEM (self)));
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disabling_context_new);
    ctx->step = DISABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  MM_IFACE_MODEM_STATE, &ctx->previous_state,
                  NULL);
    g_assert (ctx->skeleton != NULL);

    mm_iface_modem_update_state (ctx->self,
                                 MM_MODEM_STATE_DISABLING,
                                 MM_MODEM_STATE_CHANGE_REASON_USER_REQUESTED);

    return ctx;
}

static void
disabling_context_complete_and_free (DisablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);

    if (ctx->disabled)
        mm_iface_modem_update_state (ctx->self,
                                     MM_MODEM_STATE_DISABLED,
                                     MM_MODEM_STATE_CHANGE_REASON_USER_REQUESTED);
    else
        /* Fallback to previous state */
        mm_iface_modem_update_state (ctx->self,
                                     ctx->previous_state,
                                     MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);

    g_object_unref (ctx->self);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_disable_finish (MMIfaceModem *self,
                              GAsyncResult *res,
                              GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

#undef VOID_REPLY_READY_FN
#define VOID_REPLY_READY_FN(NAME)                                       \
    static void                                                         \
    NAME##_ready (MMIfaceModem *self,                                   \
                  GAsyncResult *res,                                    \
                  DisablingContext *ctx)                                \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        MM_IFACE_MODEM_GET_INTERFACE (self)->NAME##_finish (self, res, &error); \
        if (error) {                                                    \
            g_simple_async_result_take_error (ctx->result, error);      \
            disabling_context_complete_and_free (ctx);                  \
            return;                                                     \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_disabling_step (ctx);                                 \
    }

VOID_REPLY_READY_FN (disable_unsolicited_events)
VOID_REPLY_READY_FN (modem_power_down)

static void
interface_disabling_step (DisablingContext *ctx)
{
    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS:
        if (G_UNLIKELY (!unsolicited_events_supported))
            unsolicited_events_supported = (g_quark_from_static_string (
                                                UNSOLICITED_EVENTS_SUPPORTED_TAG));

        /* Only try to disable if supported */
        if (GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                  unsolicited_events_supported))) {
            if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->disable_unsolicited_events &&
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->disable_unsolicited_events_finish) {
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->disable_unsolicited_events (
                    ctx->self,
                    (GAsyncReadyCallback)disable_unsolicited_events_ready,
                    ctx);
                return;
            }
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_MODEM_POWER_DOWN:
        /* CFUN=0 is dangerous and often will shoot devices in the head (that's
         * what it's supposed to do).  So don't use CFUN=0 by default, but let
         * specific plugins use it when they know it's safe to do so.  For
         * example, CFUN=0 will often make phones turn themselves off, but some
         * dedicated devices (ex Sierra WWAN cards) will just turn off their
         * radio but otherwise still work.
         */
        if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->modem_power_down &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->modem_power_down_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->modem_power_down (
                ctx->self,
                (GAsyncReadyCallback)modem_power_down_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_CLOSE_PORT:
        /* While the modem is enabled ports are kept open, so we need to close
         * them when the modem gets disabled. As this (should) be the last
         * closing in order to get it really closed (open count = 1), it should
         * be safe to check whether they are really open before trying to close.
         */
        if (mm_serial_port_is_open (MM_SERIAL_PORT (ctx->primary)))
            mm_serial_port_close (MM_SERIAL_PORT (ctx->primary));
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        ctx->disabled = TRUE;
        disabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_disable (MMIfaceModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    interface_disabling_step (disabling_context_new (self,
                                                     callback,
                                                     user_data));
}

/*****************************************************************************/

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_OPEN_PORT,
    ENABLING_STEP_FLASH_PORT,
    ENABLING_STEP_MODEM_INIT,
    ENABLING_STEP_MODEM_POWER_UP,
    ENABLING_STEP_MODEM_AFTER_POWER_UP,
    ENABLING_STEP_FLOW_CONTROL,
    ENABLING_STEP_SUPPORTED_CHARSETS,
    ENABLING_STEP_CHARSET,
    ENABLING_STEP_SETUP_INDICATORS,
    ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    MMIfaceModem *self;
    MMAtSerialPort *primary;
    gboolean primary_open;
    EnablingStep step;
    MMModemCharset supported_charsets;
    const MMModemCharset *current_charset;
    gboolean enabled;
    GSimpleAsyncResult *result;
    MmGdbusModem *skeleton;
};

static EnablingContext *
enabling_context_new (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    EnablingContext *ctx;

    ctx = g_new0 (EnablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->primary = g_object_ref (mm_base_modem_get_port_primary (MM_BASE_MODEM (self)));
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             enabling_context_new);
    ctx->step = ENABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);

    mm_iface_modem_update_state (ctx->self,
                                 MM_MODEM_STATE_ENABLING,
                                 MM_MODEM_STATE_CHANGE_REASON_USER_REQUESTED);

    return ctx;
}

static void
enabling_context_complete_and_free (EnablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);

    if (ctx->enabled)
        mm_iface_modem_update_state (ctx->self,
                                     MM_MODEM_STATE_ENABLED,
                                     MM_MODEM_STATE_CHANGE_REASON_USER_REQUESTED);
    else {
        /* Fallback to DISABLED/LOCKED */
        mm_iface_modem_update_state (
            ctx->self,
            (mm_gdbus_modem_get_unlock_required (ctx->skeleton) == MM_MODEM_LOCK_NONE ?
             MM_MODEM_STATE_DISABLED :
             MM_MODEM_STATE_LOCKED),
            MM_MODEM_STATE_CHANGE_REASON_UNKNOWN);
        /* Close the port if enabling failed */
        if (ctx->primary_open)
            mm_serial_port_close_force (MM_SERIAL_PORT (ctx->primary));
    }

    g_object_unref (ctx->self);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_enable_finish (MMIfaceModem *self,
                              GAsyncResult *res,
                              GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

#undef VOID_REPLY_READY_FN
#define VOID_REPLY_READY_FN(NAME)                                       \
    static void                                                         \
    NAME##_ready (MMIfaceModem *self,                                   \
                  GAsyncResult *res,                                    \
                  EnablingContext *ctx)                                 \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        MM_IFACE_MODEM_GET_INTERFACE (self)->NAME##_finish (self, res, &error); \
        if (error) {                                                    \
            g_simple_async_result_take_error (ctx->result, error);      \
            enabling_context_complete_and_free (ctx);                   \
            return;                                                     \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_enabling_step (ctx);                                  \
    }

VOID_REPLY_READY_FN (modem_init);
VOID_REPLY_READY_FN (modem_power_up);
VOID_REPLY_READY_FN (modem_after_power_up);
VOID_REPLY_READY_FN (setup_flow_control);

static void
setup_indicators_ready (MMIfaceModem *self,
                        GAsyncResult *res,
                        EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_GET_INTERFACE (self)->setup_indicators_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_dbg ("Indicator control setup failed: '%s'", error->message);
        g_error_free (error);

        /* If we get an error setting up indicators, don't even bother trying to
         * enable unsolicited events. */
        ctx->step = ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS + 1;
        interface_enabling_step (ctx);
        return;
    }

    /* Indicators setup, so assume we support unsolicited events */
    g_object_set_qdata (G_OBJECT (self),
                        unsolicited_events_supported,
                        GUINT_TO_POINTER (TRUE));

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
enable_unsolicited_events_ready (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_GET_INTERFACE (self)->enable_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_dbg ("Enabling unsolicited events failed: '%s'", error->message);
        g_error_free (error);

        /* Reset support flag */
        g_object_set_qdata (G_OBJECT (self),
                            unsolicited_events_supported,
                            GUINT_TO_POINTER (FALSE));
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
load_supported_charsets_ready (MMIfaceModem *self,
                               GAsyncResult *res,
                               EnablingContext *ctx)
{
    GError *error = NULL;

    ctx->supported_charsets =
        MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_charsets_finish (self, res, &error);
    if (error) {
        mm_warn ("couldn't load Supported Charsets: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
setup_charset_ready (MMIfaceModem *self,
                     GAsyncResult *res,
                     EnablingContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_GET_INTERFACE (self)->setup_charset_finish (self, res, &error)) {
        mm_dbg ("couldn't set charset '%s': '%s'",
                mm_modem_charset_to_string (*ctx->current_charset),
                error->message);
        g_error_free (error);

        /* Will retry step with some other charset type */
    } else
        /* Done, Go on to next step */
        ctx->step++;

    interface_enabling_step (ctx);
}

static void
interface_enabling_flash_done (MMSerialPort *port,
                               GError *error,
                               gpointer user_data)
{
    EnablingContext *ctx = user_data;

    if (error) {
        g_simple_async_result_set_from_error (ctx->result, error);
        enabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static const MMModemCharset best_charsets[] = {
    MM_MODEM_CHARSET_UTF8,
    MM_MODEM_CHARSET_UCS2,
    MM_MODEM_CHARSET_8859_1,
    MM_MODEM_CHARSET_IRA,
    MM_MODEM_CHARSET_GSM,
    MM_MODEM_CHARSET_UNKNOWN
};

static void
interface_enabling_step (EnablingContext *ctx)
{
    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        /* Setup quarks if we didn't do it before */
        if (G_UNLIKELY (!indicators_checked))
            indicators_checked = (g_quark_from_static_string (
                                      INDICATORS_CHECKED_TAG));
        if (G_UNLIKELY (!unsolicited_events_supported))
            unsolicited_events_supported = (g_quark_from_static_string (
                                                UNSOLICITED_EVENTS_SUPPORTED_TAG));
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_OPEN_PORT: {
        GError *error = NULL;

        /* Open port */
        if (!mm_serial_port_open (MM_SERIAL_PORT (ctx->primary), &error)) {
            g_simple_async_result_take_error (ctx->result, error);
            enabling_context_complete_and_free (ctx);
            return;
        }
        ctx->primary_open = TRUE;
        /* Fall down to next step */
        ctx->step++;
    }

    case ENABLING_STEP_FLASH_PORT:
        /* Flash port */
        mm_serial_port_flash (MM_SERIAL_PORT (ctx->primary),
                              100,
                              FALSE,
                              interface_enabling_flash_done,
                              ctx);
        return;

    case ENABLING_STEP_MODEM_INIT:
        if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->modem_init &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->modem_init_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->modem_init (
                ctx->self,
                (GAsyncReadyCallback)modem_init_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_MODEM_POWER_UP:
        if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->modem_power_up &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->modem_power_up_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->modem_power_up (
                ctx->self,
                (GAsyncReadyCallback)modem_power_up_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_MODEM_AFTER_POWER_UP:
        if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->modem_after_power_up &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->modem_after_power_up_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->modem_after_power_up (
                ctx->self,
                (GAsyncReadyCallback)modem_after_power_up_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_FLOW_CONTROL:
        if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->setup_flow_control &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->setup_flow_control_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->setup_flow_control (
                ctx->self,
                (GAsyncReadyCallback)setup_flow_control_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SUPPORTED_CHARSETS:
        if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_charsets &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_charsets_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_charsets (
                ctx->self,
                (GAsyncReadyCallback)load_supported_charsets_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_CHARSET:
        /* Only try to set charsets if we were able to load supported ones */
        if (ctx->supported_charsets > 0 &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->setup_charset &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->setup_charset_finish) {
            gboolean next_to_try = FALSE;

            while (!next_to_try) {
                if (!ctx->current_charset)
                    /* Switch the device's charset; we prefer UTF-8, but UCS2 will do too */
                    ctx->current_charset = &best_charsets[0];
                else
                    /* Try with the next one */
                    ctx->current_charset++;

                if (*ctx->current_charset == MM_MODEM_CHARSET_UNKNOWN)
                    break;

                if (ctx->supported_charsets & (*ctx->current_charset))
                    next_to_try = TRUE;
            }

            if (next_to_try) {
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->setup_charset (
                    ctx->self,
                    *ctx->current_charset,
                    (GAsyncReadyCallback)setup_charset_ready,
                    ctx);
                return;
            }

            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Failed to find a usable modem character set");
            enabling_context_complete_and_free (ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SETUP_INDICATORS:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                   indicators_checked))) {
            /* Set the checked flag so that we don't run it again */
            g_object_set_qdata (G_OBJECT (ctx->self),
                                indicators_checked,
                                GUINT_TO_POINTER (TRUE));
            /* Initially, assume we don't support unsolicited events */
            g_object_set_qdata (G_OBJECT (ctx->self),
                                unsolicited_events_supported,
                                GUINT_TO_POINTER (FALSE));
            if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->setup_indicators &&
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->setup_indicators_finish) {
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->setup_indicators (
                    ctx->self,
                    (GAsyncReadyCallback)setup_indicators_ready,
                    ctx);
                return;
            }
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                   unsolicited_events_supported))) {
            if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->enable_unsolicited_events &&
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->enable_unsolicited_events_finish) {
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->enable_unsolicited_events (
                    ctx->self,
                    (GAsyncReadyCallback)enable_unsolicited_events_ready,
                    ctx);
                return;
            }
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        ctx->enabled = TRUE;
        enabling_context_complete_and_free (ctx);
        /* mm_serial_port_close (MM_SERIAL_PORT (ctx->port)); */
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_enable (MMIfaceModem *self,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    interface_enabling_step (enabling_context_new (self,
                                                   callback,
                                                   user_data));
}

/*****************************************************************************/

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CURRENT_CAPABILITIES,
    INITIALIZATION_STEP_MODEM_CAPABILITIES,
    INITIALIZATION_STEP_BEARERS,
    INITIALIZATION_STEP_MANUFACTURER,
    INITIALIZATION_STEP_MODEL,
    INITIALIZATION_STEP_REVISION,
    INITIALIZATION_STEP_EQUIPMENT_ID,
    INITIALIZATION_STEP_DEVICE_ID,
    INITIALIZATION_STEP_UNLOCK_REQUIRED,
    INITIALIZATION_STEP_UNLOCK_RETRIES,
    INITIALIZATION_STEP_SIM,
    INITIALIZATION_STEP_SUPPORTED_MODES,
    INITIALIZATION_STEP_SUPPORTED_BANDS,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MMIfaceModem *self;
    MMAtSerialPort *port;
    InitializationStep step;
    GSimpleAsyncResult *result;
    MmGdbusModem *skeleton;
};

static InitializationContext *
initialization_context_new (MMIfaceModem *self,
                            MMAtSerialPort *port,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    InitializationContext *ctx;

    ctx = g_new0 (InitializationContext, 1);
    ctx->self = g_object_ref (self);
    ctx->port = g_object_ref (port);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             initialization_context_new);
    ctx->step = INITIALIZATION_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);
    return ctx;
}

static void
initialization_context_complete_and_free (InitializationContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->port);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

#undef STR_REPLY_READY_FN
#define STR_REPLY_READY_FN(NAME,DISPLAY)                                \
    static void                                                         \
    load_##NAME##_ready (MMIfaceModem *self,                            \
                         GAsyncResult *res,                             \
                         InitializationContext *ctx)                    \
    {                                                                   \
        GError *error = NULL;                                           \
        gchar *val;                                                     \
                                                                        \
        val = MM_IFACE_MODEM_GET_INTERFACE (self)->load_##NAME##_finish (self, res, &error); \
        mm_gdbus_modem_set_##NAME (ctx->skeleton, val);                 \
        g_free (val);                                                   \
                                                                        \
        if (error) {                                                    \
            mm_warn ("couldn't load %s: '%s'", DISPLAY, error->message); \
            g_error_free (error);                                       \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_initialization_step (ctx);                            \
    }

#undef UINT_REPLY_READY_FN
#define UINT_REPLY_READY_FN(NAME,DISPLAY)                               \
    static void                                                         \
    load_##NAME##_ready (MMIfaceModem *self,                            \
                         GAsyncResult *res,                             \
                         InitializationContext *ctx)                    \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        mm_gdbus_modem_set_##NAME (                                     \
            ctx->skeleton,                                              \
            MM_IFACE_MODEM_GET_INTERFACE (self)->load_##NAME##_finish (self, res, &error)); \
                                                                        \
        if (error) {                                                    \
            mm_warn ("couldn't load %s: '%s'", DISPLAY, error->message); \
            g_error_free (error);                                       \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_initialization_step (ctx);                            \
    }

static void
load_current_capabilities_ready (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 InitializationContext *ctx)
{
    GError *error = NULL;

    /* We have the property in the interface bound to the property in the
     * skeleton. */
    g_object_set (self,
                  MM_IFACE_MODEM_CURRENT_CAPABILITIES,
                  MM_IFACE_MODEM_GET_INTERFACE (self)->load_current_capabilities_finish (self, res, &error),
                  NULL);

    if (error) {
        mm_warn ("couldn't load Current Capabilities: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

UINT_REPLY_READY_FN (modem_capabilities, "Modem Capabilities")
STR_REPLY_READY_FN (manufacturer, "Manufacturer")
STR_REPLY_READY_FN (model, "Model")
STR_REPLY_READY_FN (revision, "Revision")
STR_REPLY_READY_FN (equipment_identifier, "Equipment Identifier")
STR_REPLY_READY_FN (device_identifier, "Device Identifier")
UINT_REPLY_READY_FN (supported_modes, "Supported Modes")

static void
load_supported_bands_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            InitializationContext *ctx)
{
    GError *error = NULL;
    GArray *bands_array;

    bands_array = MM_IFACE_MODEM_GET_INTERFACE (self)->load_supported_bands_finish (self, res, &error);

    if (bands_array) {
        mm_gdbus_modem_set_supported_bands (ctx->skeleton,
                                            mm_common_bands_garray_to_variant (bands_array));
        mm_gdbus_modem_set_allowed_bands (ctx->skeleton,
                                          mm_common_bands_garray_to_variant (bands_array));
        g_array_unref (bands_array);
    }

    if (error) {
        mm_warn ("couldn't load Supported Bands: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
load_unlock_required_ready (MMIfaceModem *self,
                            GAsyncResult *res,
                            InitializationContext *ctx)
{
    GError *error = NULL;

    /* NOTE: we already propagated the lock state, no need to do it again */
    mm_iface_modem_unlock_check_finish (self, res, &error);
    if (error) {
        /* FATAL */
        mm_warn ("couldn't load unlock required status: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        initialization_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

UINT_REPLY_READY_FN (unlock_retries, "Unlock Retries")

static void
sim_new_ready (GAsyncInitable *initable,
               GAsyncResult *res,
               InitializationContext *ctx)
{
    MMSim *sim;
    GError *error = NULL;

    sim = mm_sim_new_finish (initable, res, &error);
    if (!sim) {
        mm_warn ("couldn't create SIM: '%s'",
                 error ? error->message : "Unknown error");
        g_clear_error (&error);
    } else {
        g_object_bind_property (sim, MM_SIM_PATH,
                                ctx->skeleton, "sim",
                                G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

        g_object_set (ctx->self,
                      MM_IFACE_MODEM_SIM, sim,
                      NULL);
        g_object_unref (sim);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
sim_reinit_ready (MMSim *sim,
                  GAsyncResult *res,
                  InitializationContext *ctx)
{
    GError *error = NULL;

    if (!mm_sim_initialize_finish (sim, res, &error)) {
        mm_warn ("SIM re-initialization failed: '%s'",
                 error ? error->message : "Unknown error");
        g_clear_error (&error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
interface_initialization_step (InitializationContext *ctx)
{
    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Load device if not done before */
        if (!mm_gdbus_modem_get_device (ctx->skeleton)) {
            gchar *device;

            g_object_get (ctx->self,
                          MM_BASE_MODEM_DEVICE, &device,
                          NULL);
            mm_gdbus_modem_set_device (ctx->skeleton, device);
            g_free (device);
        }
        /* Load driver if not done before */
        if (!mm_gdbus_modem_get_driver (ctx->skeleton)) {
            gchar *driver;

            g_object_get (ctx->self,
                          MM_BASE_MODEM_DRIVER, &driver,
                          NULL);
            mm_gdbus_modem_set_driver (ctx->skeleton, driver);
            g_free (driver);
        }
        /* Load plugin if not done before */
        if (!mm_gdbus_modem_get_plugin (ctx->skeleton)) {
            gchar *plugin;

            g_object_get (ctx->self,
                          MM_BASE_MODEM_PLUGIN, &plugin,
                          NULL);
            mm_gdbus_modem_set_plugin (ctx->skeleton, plugin);
            g_free (plugin);
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_CURRENT_CAPABILITIES:
        /* Current capabilities may change during runtime, i.e. if new firmware reloaded; but we'll
         * try to handle that by making sure the capabilities are cleared when the new firmware is
         * reloaded. So if we're asked to re-initialize, if we already have current capabilities loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_current_capabilities (ctx->skeleton) == MM_MODEM_CAPABILITY_NONE &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_current_capabilities &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_current_capabilities_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_current_capabilities (
                ctx->self,
                (GAsyncReadyCallback)load_current_capabilities_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_MODEM_CAPABILITIES:
        /* Modem capabilities are meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_modem_capabilities (ctx->skeleton) == MM_MODEM_CAPABILITY_NONE &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_modem_capabilities &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_modem_capabilities_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_modem_capabilities (
                ctx->self,
                (GAsyncReadyCallback)load_modem_capabilities_ready,
                ctx);
            return;
        }
       /* If no specific way of getting modem capabilities, assume they are
        * equal to the current capabilities */
        mm_gdbus_modem_set_modem_capabilities (
            ctx->skeleton,
            mm_gdbus_modem_get_current_capabilities (ctx->skeleton));
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_BEARERS: {
        MMBearerList *list = NULL;

        /* Bearers setup is meant to be loaded only once during the whole
         * lifetime of the modem. The list may have been created by the object
         * implementing the interface; if so use it. */
        g_object_get (ctx->self,
                      MM_IFACE_MODEM_BEARER_LIST, &list,
                      NULL);

        if (!list) {
            list = mm_bearer_list_new (1, 1);

            /* Create new default list */
            g_object_set (ctx->self,
                          MM_IFACE_MODEM_BEARER_LIST, list,
                          NULL);
        }

        if (mm_gdbus_modem_get_max_bearers (ctx->skeleton) == 0)
            mm_gdbus_modem_set_max_bearers (
                ctx->skeleton,
                mm_bearer_list_get_max (list));
        if (mm_gdbus_modem_get_max_active_bearers (ctx->skeleton) == 0)
            mm_gdbus_modem_set_max_active_bearers (
                ctx->skeleton,
                mm_bearer_list_get_max_active (list));
        g_object_unref (list);

        /* Fall down to next step */
        ctx->step++;
    }

    case INITIALIZATION_STEP_MANUFACTURER:
        /* Manufacturer is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_manufacturer (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_manufacturer &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_manufacturer_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_manufacturer (
                ctx->self,
                (GAsyncReadyCallback)load_manufacturer_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_MODEL:
        /* Model is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_model (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_model &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_model_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_model (
                ctx->self,
                (GAsyncReadyCallback)load_model_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_REVISION:
        /* Revision is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_revision (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_revision &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_revision_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_revision (
                ctx->self,
                (GAsyncReadyCallback)load_revision_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_EQUIPMENT_ID:
        /* Equipment ID is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_equipment_identifier (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_equipment_identifier &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_equipment_identifier_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_equipment_identifier (
                ctx->self,
                (GAsyncReadyCallback)load_equipment_identifier_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_DEVICE_ID:
        /* Device ID is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_device_identifier (ctx->skeleton) == NULL &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_device_identifier &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_device_identifier_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_device_identifier (
                ctx->self,
                (GAsyncReadyCallback)load_device_identifier_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_UNLOCK_REQUIRED:
        /* Only check unlock required if we were previously not unlocked */
        if (mm_gdbus_modem_get_unlock_required (ctx->skeleton) != MM_MODEM_LOCK_NONE) {
            mm_iface_modem_unlock_check (ctx->self,
                                         (GAsyncReadyCallback)load_unlock_required_ready,
                                         ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_UNLOCK_RETRIES:
        if ((MMModemLock)mm_gdbus_modem_get_unlock_required (ctx->skeleton) == MM_MODEM_LOCK_NONE) {
            /* Default to 0 when unlocked */
            mm_gdbus_modem_set_unlock_retries (ctx->skeleton, 0);
        } else {
            if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_retries &&
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_retries_finish) {
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_unlock_retries (
                    ctx->self,
                    (GAsyncReadyCallback)load_unlock_retries_ready,
                    ctx);
                return;
            }

            /* Default to 999 when we cannot check it */
            mm_gdbus_modem_set_unlock_retries (ctx->skeleton, 999);
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_SIM: {
        MMSim *sim = NULL;

        g_object_get (ctx->self,
                      MM_IFACE_MODEM_SIM, &sim,
                      NULL);
        if (!sim) {
            mm_sim_new (MM_BASE_MODEM (ctx->self),
                        NULL, /* TODO: cancellable */
                        (GAsyncReadyCallback)sim_new_ready,
                        ctx);
            return;
        }

        /* If already available the sim object, relaunch initialization.
         * This will try to load any missing property value that couldn't be
         * retrieved before due to having the SIM locked. */
        mm_sim_initialize (sim,
                           NULL, /* TODO: cancellable */
                           (GAsyncReadyCallback)sim_reinit_ready,
                           ctx);
        return;
    }

    case INITIALIZATION_STEP_SUPPORTED_MODES:
        /* Supported modes are meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (mm_gdbus_modem_get_supported_modes (ctx->skeleton) == MM_MODEM_MODE_NONE &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_modes &&
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_modes_finish) {
            MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_modes (
                ctx->self,
                (GAsyncReadyCallback)load_supported_modes_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_SUPPORTED_BANDS: {
        GArray *supported_bands;

        supported_bands = (mm_common_bands_variant_to_garray (
                               mm_gdbus_modem_get_supported_bands (ctx->skeleton)));

        /* Supported bands are meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have them loaded,
         * don't try to load them again. */
        if (supported_bands->len == 0 ||
            g_array_index (supported_bands, MMModemBand, 0)  == MM_MODEM_BAND_UNKNOWN) {
            if (MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_bands &&
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_bands_finish) {
                MM_IFACE_MODEM_GET_INTERFACE (ctx->self)->load_supported_bands (
                    ctx->self,
                    (GAsyncReadyCallback)load_supported_bands_ready,
                    ctx);
                g_array_unref (supported_bands);
                return;
            }

            /* Loading supported bands not implemented, default to ANY */
            mm_gdbus_modem_set_supported_bands (ctx->skeleton, mm_common_build_bands_any ());
            mm_gdbus_modem_set_allowed_bands (ctx->skeleton, mm_common_build_bands_any ());
        }
        g_array_unref (supported_bands);

        /* Fall down to next step */
        ctx->step++;
    }

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-create-bearer",
                          G_CALLBACK (handle_create_bearer),
                          ctx->self);
        g_signal_connect (ctx->skeleton,
                          "handle-delete-bearer",
                          G_CALLBACK (handle_delete_bearer),
                          ctx->self);
        g_signal_connect (ctx->skeleton,
                          "handle-list-bearers",
                          G_CALLBACK (handle_list_bearers),
                          ctx->self);
        g_signal_connect (ctx->skeleton,
                          "handle-enable",
                          G_CALLBACK (handle_enable),
                          ctx->self);
        g_signal_connect (ctx->skeleton,
                          "handle-reset",
                          G_CALLBACK (handle_reset),
                          ctx->self);
        g_signal_connect (ctx->skeleton,
                          "handle-factory-reset",
                          G_CALLBACK (handle_factory_reset),
                          ctx->self);
        g_signal_connect (ctx->skeleton,
                          "handle-set-allowed-bands",
                          G_CALLBACK (handle_set_allowed_bands),
                          ctx->self);
        g_signal_connect (ctx->skeleton,
                          "handle-set-allowed-modes",
                          G_CALLBACK (handle_set_allowed_modes),
                          ctx->self);

        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem (MM_GDBUS_OBJECT_SKELETON (ctx->self),
                                            MM_GDBUS_MODEM (ctx->skeleton));

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        initialization_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_initialize_finish (MMIfaceModem *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    g_return_val_if_fail (MM_IS_IFACE_MODEM (self), FALSE);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);

    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_iface_modem_initialize (MMIfaceModem *self,
                           MMAtSerialPort *port,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    MmGdbusModem *skeleton = NULL;

    g_return_if_fail (MM_IS_IFACE_MODEM (self));

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem_skeleton_new ();

        /* Set all initial property defaults */
        mm_gdbus_modem_set_sim (skeleton, NULL);
        mm_gdbus_modem_set_modem_capabilities (skeleton, MM_MODEM_CAPABILITY_NONE);
        mm_gdbus_modem_set_max_bearers (skeleton, 0);
        mm_gdbus_modem_set_max_active_bearers (skeleton, 0);
        mm_gdbus_modem_set_manufacturer (skeleton, NULL);
        mm_gdbus_modem_set_model (skeleton, NULL);
        mm_gdbus_modem_set_revision (skeleton, NULL);
        mm_gdbus_modem_set_device_identifier (skeleton, NULL);
        mm_gdbus_modem_set_device (skeleton, NULL);
        mm_gdbus_modem_set_driver (skeleton, NULL);
        mm_gdbus_modem_set_plugin (skeleton, NULL);
        mm_gdbus_modem_set_equipment_identifier (skeleton, NULL);
        mm_gdbus_modem_set_unlock_required (skeleton, MM_MODEM_LOCK_UNKNOWN);
        mm_gdbus_modem_set_unlock_retries (skeleton, 0);
        mm_gdbus_modem_set_access_technologies (skeleton, MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN);
        mm_gdbus_modem_set_signal_quality (skeleton, g_variant_new ("(ub)", 0, FALSE));
        mm_gdbus_modem_set_supported_modes (skeleton, MM_MODEM_MODE_NONE);
        mm_gdbus_modem_set_allowed_modes (skeleton, MM_MODEM_MODE_ANY);
        mm_gdbus_modem_set_preferred_mode (skeleton, MM_MODEM_MODE_NONE);
        mm_gdbus_modem_set_supported_bands (skeleton, mm_common_build_bands_unknown ());
        mm_gdbus_modem_set_allowed_bands (skeleton, mm_common_build_bands_unknown ());

        /* Bind our State property */
        g_object_bind_property (self, MM_IFACE_MODEM_STATE,
                                skeleton, "state",
                                G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
        /* Bind our Capabilities property */
        g_object_bind_property (self, MM_IFACE_MODEM_CURRENT_CAPABILITIES,
                                skeleton, "current-capabilities",
                                G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

        g_object_set (self,
                      MM_IFACE_MODEM_DBUS_SKELETON, skeleton,
                      NULL);
    }

    /* Perform async initialization here */
    interface_initialization_step (initialization_context_new (self,
                                                               port,
                                                               callback,
                                                               user_data));
    g_object_unref (skeleton);
    return;
}

void
mm_iface_modem_shutdown (MMIfaceModem *self)
{
    g_return_if_fail (MM_IS_IFACE_MODEM (self));

    /* Remove SIM object */
    g_object_set (self,
                  MM_IFACE_MODEM_SIM, NULL,
                  NULL);
    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

gboolean
mm_iface_modem_is_3gpp (MMIfaceModem *self)
{
    MMModemCapability capabilities = MM_MODEM_CAPABILITY_NONE;

    g_object_get (self,
                  MM_IFACE_MODEM_CURRENT_CAPABILITIES, &capabilities,
                  NULL);

    return (capabilities & MM_MODEM_CAPABILITY_3GPP);
}

/*****************************************************************************/

static void
iface_modem_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_DBUS_SKELETON,
                              "Modem DBus skeleton",
                              "DBus skeleton for the Modem interface",
                              MM_GDBUS_TYPE_MODEM_SKELETON,
                              G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_SIM,
                              "SIM",
                              "SIM object",
                              MM_TYPE_SIM,
                              G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_enum (MM_IFACE_MODEM_STATE,
                            "State",
                            "State of the modem",
                            MM_TYPE_MODEM_STATE,
                            MM_MODEM_STATE_UNKNOWN,
                            G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_flags (MM_IFACE_MODEM_CURRENT_CAPABILITIES,
                             "Current capabilities",
                             "Current capabilities of the modem",
                             MM_TYPE_MODEM_CAPABILITY,
                             MM_MODEM_CAPABILITY_NONE,
                             G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_BEARER_LIST,
                              "Bearer list",
                              "List of bearers handled by the modem",
                              MM_TYPE_BEARER_LIST,
                              G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_get_type (void)
{
    static GType iface_modem_type = 0;

    if (!G_UNLIKELY (iface_modem_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModem), /* class_size */
            iface_modem_init,      /* base_init */
            NULL,                  /* base_finalize */
        };

        iface_modem_type = g_type_register_static (G_TYPE_INTERFACE,
                                                   "MMIfaceModem",
                                                   &info,
                                                   0);

        g_type_interface_add_prerequisite (iface_modem_type, MM_TYPE_BASE_MODEM);
    }

    return iface_modem_type;
}
