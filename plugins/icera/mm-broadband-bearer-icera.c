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
 * Copyright (C) 2012 Google, Inc.
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-broadband-bearer-icera.h"
#include "mm-base-modem-at.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"
#include "mm-utils.h"

G_DEFINE_TYPE (MMBroadbandBearerIcera, mm_broadband_bearer_icera, MM_TYPE_BROADBAND_BEARER);

struct _MMBroadbandBearerIceraPrivate {
    /* Connection related */
    gpointer connect_pending;
    guint connect_pending_id;
    gulong connect_cancellable_id;
};

/*****************************************************************************/
/* 3GPP Dialing (sub-step of the 3GPP Connection sequence) */

typedef struct {
    MMBroadbandBearerIcera *self;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    guint cid;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;
} Dial3gppContext;

static Dial3gppContext *
dial_3gpp_context_new (MMBroadbandBearerIcera *self,
                       MMBaseModem *modem,
                       MMAtSerialPort *primary,
                       guint cid,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    Dial3gppContext *ctx;

    ctx = g_new0 (Dial3gppContext, 1);
    ctx->self = g_object_ref (self);
    ctx->modem = g_object_ref (modem);
    ctx->primary = g_object_ref (primary);
    ctx->cid = cid;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             dial_3gpp_context_new);
    ctx->cancellable = g_object_ref (cancellable);
    return ctx;
}

static void
dial_3gpp_context_complete_and_free (Dial3gppContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->result);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
dial_3gpp_context_set_error_if_cancelled (Dial3gppContext *ctx,
                                          GError **error)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_CANCELLED,
                 "Dial operation has been cancelled");
    return TRUE;
}

static gboolean
dial_3gpp_context_complete_and_free_if_cancelled (Dial3gppContext *ctx)
{
    GError *error = NULL;

    if (!dial_3gpp_context_set_error_if_cancelled (ctx, &error))
        return FALSE;

    g_simple_async_result_take_error (ctx->result, error);
    dial_3gpp_context_complete_and_free (ctx);
    return TRUE;
}

static gboolean
dial_3gpp_finish (MMBroadbandBearer *self,
                  GAsyncResult *res,
                  GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
connect_reset_ready (MMBaseModem *modem,
                     GAsyncResult *res,
                     Dial3gppContext *ctx)
{
    mm_base_modem_at_command_full_finish (modem, res, NULL);

    /* error should have already been set in the simple async result */
    dial_3gpp_context_complete_and_free (ctx);
}

static void
connect_reset (Dial3gppContext *ctx)
{
    gchar *command;

    /* Need to reset the connection attempt */
    command = g_strdup_printf ("%%IPDPACT=%d,0", ctx->cid);
    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   3,
                                   FALSE,
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)connect_reset_ready,
                                   ctx);
    g_free (command);
}

static gboolean
connect_timed_out_cb (MMBroadbandBearerIcera *self)
{
    Dial3gppContext *ctx;

    /* Recover context and remove cancellation */
    ctx = self->priv->connect_pending;

    g_cancellable_disconnect (ctx->cancellable,
                              self->priv->connect_cancellable_id);

    self->priv->connect_pending = NULL;
    self->priv->connect_pending_id = 0;
    self->priv->connect_cancellable_id = 0;

    g_simple_async_result_set_error (ctx->result,
                                     MM_MOBILE_EQUIPMENT_ERROR,
                                     MM_MOBILE_EQUIPMENT_ERROR_NETWORK_TIMEOUT,
                                     "Connection attempt timed out");
    connect_reset (ctx);

    return FALSE;
}

static void
connect_cancelled_cb (GCancellable *cancellable,
                      MMBroadbandBearerIcera *self)
{
    GError *error = NULL;
    Dial3gppContext *ctx;

    /* Recover context and remove timeout */
    ctx = self->priv->connect_pending;

    g_source_remove (self->priv->connect_pending_id);

    self->priv->connect_pending = NULL;
    self->priv->connect_pending_id = 0;
    self->priv->connect_cancellable_id = 0;

    g_assert (dial_3gpp_context_set_error_if_cancelled (ctx, &error));

    g_simple_async_result_take_error (ctx->result, error);
    connect_reset (ctx);
}

static void
report_connect_status (MMBroadbandBearerIcera *self,
                       MMBroadbandBearerIceraConnectionStatus status)
{
    Dial3gppContext *ctx;

    /* Recover context */
    ctx = self->priv->connect_pending;
    self->priv->connect_pending = NULL;

    /* Cleanup cancellable and timeout, if any */
    if (self->priv->connect_pending_id) {
        g_source_remove (self->priv->connect_pending_id);
        self->priv->connect_pending_id = 0;
    }

    if (self->priv->connect_cancellable_id) {
        g_cancellable_disconnect (ctx->cancellable,
                                  self->priv->connect_cancellable_id);
        self->priv->connect_cancellable_id = 0;
    }

    switch (status) {
    case MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_UNKNOWN:
        g_warn_if_reached ();
        break;

    case MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_CONNECTED:
        if (!ctx)
            break;

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        dial_3gpp_context_complete_and_free (ctx);
        return;

    case MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_CONNECTION_FAILED:
        if (!ctx)
            break;

        g_simple_async_result_set_error (ctx->result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_FAILED,
                                         "Call setup failed");
        dial_3gpp_context_complete_and_free (ctx);
        return;

    case MM_BROADBAND_BEARER_ICERA_CONNECTION_STATUS_DISCONNECTED:
        if (ctx) {
            g_simple_async_result_set_error (ctx->result,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Call setup failed");
            dial_3gpp_context_complete_and_free (ctx);
        } else {
            /* Just ensure we mark ourselves as being disconnected... */
            mm_bearer_report_disconnection (MM_BEARER (self));
        }
        break;
    }
}

static void
activate_ready (MMBaseModem *modem,
                GAsyncResult *res,
                Dial3gppContext *ctx)
{
    GError *error = NULL;

    /* From now on, if we get cancelled, we'll need to run the connection
     * reset ourselves just in case */

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    /* We will now setup a timeout and keep the context in the bearer's private.
     * Reports of modem being connected will arrive via unsolicited messages. */
    ctx->self->priv->connect_pending_id = g_timeout_add_seconds (60,
                                                                 (GSourceFunc)connect_timed_out_cb,
                                                                 ctx->self);
    ctx->self->priv->connect_cancellable_id = g_cancellable_connect (ctx->cancellable,
                                                                     G_CALLBACK (connect_cancelled_cb),
                                                                     ctx->self,
                                                                     NULL);
}

static void
deactivate_ready (MMBaseModem *modem,
                  GAsyncResult *res,
                  Dial3gppContext *ctx)
{
    gchar *command;

    /*
     * Ignore any error here; %IPDPACT=ctx,0 will produce an error 767
     * if the context is not, in fact, connected. This is annoying but
     * harmless.
     */
    mm_base_modem_at_command_full_finish (modem, res, NULL);

    command = g_strdup_printf ("%%IPDPACT=%d,1", ctx->cid);
    mm_base_modem_at_command_full (
        ctx->modem,
        ctx->primary,
        command,
        60,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)activate_ready,
        ctx);
    g_free (command);

    /* The unsolicited response to %IPDPACT may come before the OK does */
    g_assert (ctx->self->priv->connect_pending == NULL);
    ctx->self->priv->connect_pending = ctx;
}

static void
authenticate_ready (MMBaseModem *modem,
                    GAsyncResult *res,
                    Dial3gppContext *ctx)
{
    GError *error = NULL;
    gchar *command;

    /* If cancelled, complete */
    if (dial_3gpp_context_complete_and_free_if_cancelled (ctx))
        return;

    if (!mm_base_modem_at_command_full_finish (modem, res, &error)) {
        /* TODO(njw): retry up to 3 times with a 1-second delay */
        /* Return an error */
        g_simple_async_result_take_error (ctx->result, error);
        dial_3gpp_context_complete_and_free (ctx);
        return;
    }

    /*
     * Deactivate the context we want to use before we try to activate
     * it. This handles the case where ModemManager crashed while
     * connected and is now trying to reconnect. (Should some part of
     * the core or modem driver have made sure of this already?)
     */
    command = g_strdup_printf ("%%IPDPACT=%d,0", ctx->cid);
    mm_base_modem_at_command_full (
        ctx->modem,
        ctx->primary,
        command,
        60,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)deactivate_ready,
        ctx);
    g_free (command);
}

static void
authenticate (Dial3gppContext *ctx)
{
    gchar *command;
    const gchar *user;
    const gchar *password;

    user = mm_bearer_properties_get_user (mm_bearer_peek_config (MM_BEARER (ctx->self)));
    password = mm_bearer_properties_get_password (mm_bearer_peek_config (MM_BEARER (ctx->self)));

    /* Both user and password are required; otherwise firmware returns an error */
    if (!user || !password)
		command = g_strdup_printf ("%%IPDPCFG=%d,0,0,\"\",\"\"", ctx->cid);
    else {
        gchar *quoted_user;
        gchar *quoted_password;

        quoted_user = mm_at_serial_port_quote_string (user);
        quoted_password = mm_at_serial_port_quote_string (password);
        command = g_strdup_printf ("%%IPDPCFG=%d,0,1,%s,%s",
                                   ctx->cid, quoted_user, quoted_password);
        g_free (quoted_user);
        g_free (quoted_password);
    }

    mm_base_modem_at_command_full (ctx->modem,
                                   ctx->primary,
                                   command,
                                   60,
                                   FALSE,
                                   NULL, /* cancellable */
                                   (GAsyncReadyCallback)authenticate_ready,
                                   ctx);
    g_free (command);
}

static void
dial_3gpp (MMBroadbandBearer *self,
           MMBaseModem *modem,
           MMAtSerialPort *primary,
           guint cid,
           GCancellable *cancellable,
           GAsyncReadyCallback callback,
           gpointer user_data)
{
    g_assert (primary != NULL);

    authenticate (dial_3gpp_context_new (MM_BROADBAND_BEARER_ICERA (self),
                                         modem,
                                         primary,
                                         cid,
                                         cancellable,
                                         callback,
                                         user_data));
}

/*****************************************************************************/

void
mm_broadband_bearer_icera_report_connection_status (MMBroadbandBearerIcera *self,
                                                    MMBroadbandBearerIceraConnectionStatus status)
{
    if (self->priv->connect_pending)
        report_connect_status (self, status);
}

/*****************************************************************************/

MMBearer *
mm_broadband_bearer_icera_new_finish (GAsyncResult *res,
                                      GError **error)
{
    GObject *source;
    GObject *bearer;

    source = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!bearer)
        return NULL;

    /* Only export valid bearers */
    mm_bearer_export (MM_BEARER (bearer));

    return MM_BEARER (bearer);
}

void
mm_broadband_bearer_icera_new (MMBroadbandModem *modem,
                               MMBearerProperties *config,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER_ICERA,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BEARER_MODEM, modem,
        MM_BEARER_CONFIG, config,
        NULL);
}

static void
mm_broadband_bearer_icera_init (MMBroadbandBearerIcera *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_BEARER_ICERA,
                                              MMBroadbandBearerIceraPrivate);
}

static void
mm_broadband_bearer_icera_class_init (MMBroadbandBearerIceraClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandBearerClass *broadband_bearer_class = MM_BROADBAND_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandBearerIceraPrivate));

    broadband_bearer_class->dial_3gpp = dial_3gpp;
    broadband_bearer_class->dial_3gpp_finish = dial_3gpp_finish;
}
