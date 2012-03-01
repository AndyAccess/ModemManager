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
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2011 - 2012 Google, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-broadband-bearer.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-iface-modem-cdma.h"
#include "mm-base-modem-at.h"
#include "mm-utils.h"
#include "mm-log.h"
#include "mm-modem-helpers.h"

static void async_initable_iface_init (GAsyncInitableIface *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandBearer, mm_broadband_bearer, MM_TYPE_BEARER, 0,
                        G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                                               async_initable_iface_init));

typedef enum {
    CONNECTION_FORBIDDEN_REASON_NONE,
    CONNECTION_FORBIDDEN_REASON_UNREGISTERED,
    CONNECTION_FORBIDDEN_REASON_ROAMING,
    CONNECTION_FORBIDDEN_REASON_LAST
} ConnectionForbiddenReason;

typedef enum {
    CONNECTION_TYPE_NONE,
    CONNECTION_TYPE_3GPP,
    CONNECTION_TYPE_CDMA,
} ConnectionType;

enum {
    PROP_0,
    PROP_3GPP_APN,
    PROP_CDMA_NUMBER,
    PROP_CDMA_RM_PROTOCOL,
    PROP_IP_TYPE,
    PROP_ALLOW_ROAMING,
    PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

struct _MMBroadbandBearerPrivate {
    /*-- Common stuff --*/
    /* IP type  */
    gchar *ip_type;
    /* Flag to allow/forbid connections while roaming */
    gboolean allow_roaming;
    /* Data port used when modem is connected */
    MMPort *port;
    /* Current connection type */
    ConnectionType connection_type;

    /*-- 3GPP specific --*/
    /* Reason if 3GPP connection is forbidden */
    ConnectionForbiddenReason reason_3gpp;
    /* Handler ID for the registration state change signals */
    guint id_3gpp_registration_change;
    /* APN of the PDP context */
    gchar *apn;
    /* CID of the PDP context */
    guint cid;

    /*-- CDMA specific --*/
    /* Reason if CDMA connection is forbidden */
    ConnectionForbiddenReason reason_cdma;
    /* Handler IDs for the registration state change signals */
    guint id_cdma1x_registration_change;
    guint id_evdo_registration_change;
    /* (Optional) Number to dial */
    gchar *number;
    /* Protocol of the Rm interface */
    MMModemCdmaRmProtocol rm_protocol;
};

/*****************************************************************************/

static const gchar *connection_forbidden_reason_str [CONNECTION_FORBIDDEN_REASON_LAST] = {
    "none",
    "Not registered in the network",
    "Registered in roaming network, and roaming not allowed"
};

/*****************************************************************************/

const gchar *
mm_broadband_bearer_get_3gpp_apn (MMBroadbandBearer *self)
{
    return self->priv->apn;
}

MMModemCdmaRmProtocol
mm_broadband_bearer_get_cdma_rm_protocol (MMBroadbandBearer *self)
{
    return self->priv->rm_protocol;
}

const gchar *
mm_broadband_bearer_get_ip_type (MMBroadbandBearer *self)
{
    return self->priv->ip_type;
}

gboolean
mm_broadband_bearer_get_allow_roaming (MMBroadbandBearer *self)
{
    return self->priv->allow_roaming;
}

/*****************************************************************************/
/* Detailed connect context, used in both CDMA and 3GPP sequences */
typedef struct {
    MMBroadbandBearer *self;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    MMPort *data;
    GCancellable *cancellable;
    GSimpleAsyncResult *result;

    /* 3GPP-specific */
    guint cid;
    guint max_cid;
    GError *saved_error;
} DetailedConnectContext;

static gboolean
detailed_connect_finish (MMBroadbandBearer *self,
                         GAsyncResult *res,
                         MMCommonBearerIpConfig **ipv4_config,
                         MMCommonBearerIpConfig **ipv6_config,
                         GError **error)
{
    MMCommonBearerIpConfig *config;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    config = g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));

    /* In the default implementation, we assume we'll have the same configs */
    *ipv4_config = g_object_ref (config);
    *ipv6_config = g_object_ref (config);
    return TRUE;
}

static void
detailed_connect_context_complete_and_free (DetailedConnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    if (ctx->saved_error)
        g_error_free (ctx->saved_error);
    g_object_unref (ctx->cancellable);
    g_object_unref (ctx->data);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->self);
    g_object_unref (ctx->modem);
    g_free (ctx);
}

static void
detailed_connect_context_complete_and_free_successful (DetailedConnectContext *ctx)
{
    MMCommonBearerIpConfig *config;

    /* If serial port, set PPP method. Otherwise, assume DHCP is needed. */
    config = mm_common_bearer_ip_config_new ();
    mm_common_bearer_ip_config_set_method (config,
                                           (MM_IS_AT_SERIAL_PORT (ctx->data) ?
                                            MM_BEARER_IP_METHOD_PPP :
                                            MM_BEARER_IP_METHOD_DHCP));
    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               config,
                                               (GDestroyNotify)g_object_unref);
    detailed_connect_context_complete_and_free (ctx);
}

static gboolean
detailed_connect_context_set_error_if_cancelled (DetailedConnectContext *ctx,
                                                 GError **error)
{
    if (!g_cancellable_is_cancelled (ctx->cancellable))
        return FALSE;

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_CANCELLED,
                 "Connection setup operation has been cancelled");
    return TRUE;
}

static gboolean
detailed_connect_context_complete_and_free_if_cancelled (DetailedConnectContext *ctx)
{
    GError *error = NULL;

    if (!detailed_connect_context_set_error_if_cancelled (ctx, &error))
        return FALSE;

    g_simple_async_result_take_error (ctx->result, error);
    detailed_connect_context_complete_and_free (ctx);
    return TRUE;
}

static DetailedConnectContext *
detailed_connect_context_new (MMBroadbandBearer *self,
                              MMBroadbandModem *modem,
                              MMAtSerialPort *primary,
                              MMPort *data,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
    DetailedConnectContext *ctx;

    ctx = g_new0 (DetailedConnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->modem = MM_BASE_MODEM (g_object_ref (modem));
    ctx->primary = g_object_ref (primary);
    ctx->data = g_object_ref (data);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             detailed_connect_context_new);
    /* NOTE:
     * We don't currently support cancelling AT commands, so we'll just check
     * whether the operation is to be cancelled at each step. */
    ctx->cancellable = g_object_ref (cancellable);
    return ctx;
}

/*****************************************************************************/
/* CDMA CONNECT
 *
 * CDMA connection procedure of a bearer involves several steps:
 * 1) Get data port from the modem. Default implementation will have only
 *    one single possible data port, but plugins may have more.
 * 2) If requesting specific RM, load current.
 *  2.1) If current RM different to the requested one, set the new one.
 * 3) Initiate call.
 */

static void
dial_cdma_ready (MMBaseModem *modem,
                 GAsyncResult *res,
                 DetailedConnectContext *ctx)
{
    GError *error = NULL;

    /* DO NOT check for cancellable here. If we got here without errors, the
     * bearer is really connected and therefore we need to reflect that in
     * the state machine. */
    mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        mm_warn ("Couldn't connect: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    /* else... Yuhu! */
    ctx->self->priv->connection_type = CONNECTION_TYPE_CDMA;
    detailed_connect_context_complete_and_free_successful (ctx);
}

static void
cdma_connect_context_dial (DetailedConnectContext *ctx)
{
    gchar *command;

    /* If a number was given when creating the bearer, use that one.
     * Otherwise, use the default one, #777
     */
    if (ctx->self->priv->number)
        command = g_strconcat ("DT", ctx->self->priv->number, NULL);
    else
        command = g_strdup ("DT#777");
    mm_base_modem_at_command_in_port (
        ctx->modem,
        ctx->primary,
        command,
        90,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)dial_cdma_ready,
        ctx);
    g_free (command);
}

static void
set_rm_protocol_ready (MMBaseModem *self,
                       GAsyncResult *res,
                       DetailedConnectContext *ctx)
{
    GError *error = NULL;

    /* If cancelled, complete */
    if (detailed_connect_context_complete_and_free_if_cancelled (ctx))
        return;

    mm_base_modem_at_command_finish (self, res, &error);
    if (error) {
        mm_warn ("Couldn't set RM protocol: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    /* Nothing else needed, go on with dialing */
    cdma_connect_context_dial (ctx);
}

static void
current_rm_protocol_ready (MMBaseModem *self,
                           GAsyncResult *res,
                           DetailedConnectContext *ctx)
{
    const gchar *result;
    GError *error = NULL;
    guint current_index;
    MMModemCdmaRmProtocol current_rm;

    /* If cancelled, complete */
    if (detailed_connect_context_complete_and_free_if_cancelled (ctx))
        return;

    result = mm_base_modem_at_command_finish (self, res, &error);
    if (error) {
        mm_warn ("Couldn't query current RM protocol: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    result = mm_strip_tag (result, "+CRM:");
    current_index = (guint) atoi (result);
    current_rm = mm_cdma_get_rm_protocol_from_index (current_index, &error);
    if (error) {
        mm_warn ("Couldn't parse RM protocol reply (%s): '%s'",
                 result,
                 error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    if (current_rm != ctx->self->priv->rm_protocol) {
        guint new_index;
        gchar *command;

        mm_dbg ("Setting requested RM protocol...");

        new_index = (mm_cdma_get_index_from_rm_protocol (
                         ctx->self->priv->rm_protocol,
                         &error));
        if (error) {
            mm_warn ("Cannot set RM protocol: '%s'",
                     error->message);
            g_simple_async_result_take_error (ctx->result, error);
            detailed_connect_context_complete_and_free (ctx);
            return;
        }

        command = g_strdup_printf ("+CRM=%u", new_index);
        mm_base_modem_at_command_in_port (
            ctx->modem,
            ctx->primary,
            command,
            3,
            FALSE,
            NULL, /* cancellable */
            (GAsyncReadyCallback)set_rm_protocol_ready,
            ctx);
        g_free (command);
        return;
    }

    /* Nothing else needed, go on with dialing */
    cdma_connect_context_dial (ctx);
}

static void
connect_cdma (MMBroadbandBearer *self,
              MMBroadbandModem *modem,
              MMAtSerialPort *primary,
              MMAtSerialPort *secondary, /* unused by us */
              MMPort *data,
              GCancellable *cancellable,
              GAsyncReadyCallback callback,
              gpointer user_data)
{
    DetailedConnectContext *ctx;

    ctx = detailed_connect_context_new (self,
                                        modem,
                                        primary,
                                        data,
                                        cancellable,
                                        callback,
                                        user_data);

    if (self->priv->rm_protocol != MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN) {
        /* Need to query current RM protocol */
        mm_dbg ("Querying current RM protocol set...");
        mm_base_modem_at_command_in_port (
            ctx->modem,
            ctx->primary,
            "+CRM?",
            3,
            FALSE,
            NULL, /* cancellable */
            (GAsyncReadyCallback)current_rm_protocol_ready,
            ctx);
        return;
    }

    /* Nothing else needed, go on with dialing */
    cdma_connect_context_dial (ctx);
}

/*****************************************************************************/
/* 3GPP CONNECT
 *
 * 3GPP connection procedure of a bearer involves several steps:
 * 1) Get data port from the modem. Default implementation will have only
 *    one single possible data port, but plugins may have more.
 * 2) Decide which PDP context to use
 *   2.1) Look for an already existing PDP context with the same APN.
 *   2.2) If none found with the same APN, try to find a PDP context without any
 *        predefined APN.
 *   2.3) If none found, look for the highest available CID, and use that one.
 * 3) Activate PDP context.
 * 4) Initiate call.
 */

static void
connect_report_ready (MMBaseModem *modem,
                      GAsyncResult *res,
                      DetailedConnectContext *ctx)
{
    const gchar *result;

    /* If cancelled, complete */
    if (detailed_connect_context_complete_and_free_if_cancelled (ctx))
        return;

    result = mm_base_modem_at_command_finish (modem, res, NULL);
    if (result &&
        g_str_has_prefix (result, "+CEER: ") &&
        strlen (result) > 7) {
        g_simple_async_result_set_error (ctx->result,
                                         ctx->saved_error->domain,
                                         ctx->saved_error->code,
                                         "%s", &result[7]);
    } else
        g_simple_async_result_take_error (ctx->result,
                                          ctx->saved_error);

    g_error_free (ctx->saved_error);
    ctx->saved_error = NULL;

    /* Done with errors */
    detailed_connect_context_complete_and_free (ctx);
}

static void
dial_3gpp_ready (MMBaseModem *modem,
                 GAsyncResult *res,
                 DetailedConnectContext *ctx)
{
    /* DO NOT check for cancellable here. If we got here without errors, the
     * bearer is really connected and therefore we need to reflect that in
     * the state machine. */
    mm_base_modem_at_command_finish (modem, res, &(ctx->saved_error));
    if (ctx->saved_error) {
        /* Try to get more information why it failed */
        mm_base_modem_at_command_in_port (
            modem,
            ctx->primary,
            "+CEER",
            3,
            FALSE,
            NULL, /* cancellable */
            (GAsyncReadyCallback)connect_report_ready,
            ctx);
        return;
    }

    /* Keep data port and CID around while connected */
    ctx->self->priv->connection_type = CONNECTION_TYPE_3GPP;
    ctx->self->priv->cid = ctx->cid;
    ctx->self->priv->port = g_object_ref (ctx->data);

    /* Yuhu! */
    detailed_connect_context_complete_and_free_successful (ctx);
}

static void
initialize_pdp_context_ready (MMBaseModem *self,
                              GAsyncResult *res,
                              DetailedConnectContext *ctx)
{
    GError *error = NULL;
    gchar *command;

    /* If cancelled, complete */
    if (detailed_connect_context_complete_and_free_if_cancelled (ctx))
        return;

    mm_base_modem_at_command_finish (self, res, &error);
    if (error) {
        mm_warn ("Couldn't initialize PDP context with our APN: '%s'",
                 error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    /* Use default *99 to connect */
    command = g_strdup_printf ("ATD*99***%d#", ctx->cid);
    mm_base_modem_at_command_in_port (
        ctx->modem,
        ctx->primary,
        command,
        60,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)dial_3gpp_ready,
        ctx);
    g_free (command);
}

static void
find_cid_ready (MMBaseModem *self,
                GAsyncResult *res,
                DetailedConnectContext *ctx)
{
    GVariant *result;
    gchar *command;
    GError *error = NULL;

    result = mm_base_modem_at_sequence_finish (self, res, NULL, &error);
    if (!result) {
        mm_warn ("Couldn't find best CID to use: '%s'", error->message);
        g_simple_async_result_take_error (ctx->result, error);
        detailed_connect_context_complete_and_free (ctx);
        return;
    }

    /* If cancelled, complete. Normally, we would get the cancellation error
     * already when finishing the sequence, but we may still get cancelled
     * between last command result parsing in the sequence and the ready(). */
    if (detailed_connect_context_complete_and_free_if_cancelled (ctx))
        return;

    /* Initialize PDP context with our APN */
    ctx->cid = g_variant_get_uint32 (result);
    command = g_strdup_printf ("+CGDCONT=%u,\"IP\",\"%s\"",
                               ctx->cid,
                               ctx->self->priv->apn);
    mm_base_modem_at_command_in_port (
        ctx->modem,
        ctx->primary,
        command,
        3,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)initialize_pdp_context_ready,
        ctx);
    g_free (command);
}

static gboolean
parse_cid_range (MMBaseModem *self,
                 DetailedConnectContext *ctx,
                 const gchar *command,
                 const gchar *response,
                 gboolean last_command,
                 const GError *error,
                 GVariant **result,
                 GError **result_error)
{
    GError *inner_error = NULL;
    GRegex *r;
    GMatchInfo *match_info;
    guint cid = 0;

    /* If cancelled, set result error */
    if (detailed_connect_context_set_error_if_cancelled (ctx, result_error))
        return FALSE;

    if (error) {
        mm_dbg ("Unexpected +CGDCONT error: '%s'", error->message);
        mm_dbg ("Defaulting to CID=1");
        *result = g_variant_new_uint32 (1);
        return TRUE;
    }

    if (!g_str_has_prefix (response, "+CGDCONT:")) {
        mm_dbg ("Unexpected +CGDCONT response: '%s'", response);
        mm_dbg ("Defaulting to CID=1");
        *result = g_variant_new_uint32 (1);
        return TRUE;
    }

    r = g_regex_new ("\\+CGDCONT:\\s*\\((\\d+)-(\\d+)\\),\\(?\"(\\S+)\"",
                     G_REGEX_DOLLAR_ENDONLY | G_REGEX_RAW,
                     0, &inner_error);
    if (r) {
        g_regex_match_full (r, response, strlen (response), 0, 0, &match_info, &inner_error);
        cid = 0;
        while (!inner_error &&
               cid == 0 &&
               g_match_info_matches (match_info)) {
            gchar *pdp_type;

            pdp_type = g_match_info_fetch (match_info, 3);

            /* TODO: What about PDP contexts of type "IPV6"? */
            if (g_str_equal (pdp_type, "IP")) {
                gchar *max_cid_range_str;
                guint max_cid_range;

                max_cid_range_str = g_match_info_fetch (match_info, 2);
                max_cid_range = (guint)atoi (max_cid_range_str);

                if (ctx->max_cid < max_cid_range)
                    cid = ctx->max_cid + 1;
                else
                    cid = ctx->max_cid;

                g_free (max_cid_range_str);
            }

            g_free (pdp_type);
            g_match_info_next (match_info, &inner_error);
        }

        g_match_info_free (match_info);
        g_regex_unref (r);
    }

    if (inner_error) {
        mm_dbg ("Unexpected error matching +CGDCONT response: '%s'", inner_error->message);
        g_error_free (inner_error);
    }

    if (cid == 0) {
        mm_dbg ("Defaulting to CID=1");
        cid = 1;
    } else
        mm_dbg ("Using CID %u", cid);

    *result = g_variant_new_uint32 (cid);
    return TRUE;
}

static gboolean
parse_pdp_list (MMBaseModem *self,
                DetailedConnectContext *ctx,
                const gchar *command,
                const gchar *response,
                gboolean last_command,
                const GError *error,
                GVariant **result,
                GError **result_error)
{
    GError *inner_error = NULL;
    GList *pdp_list;
    GList *l;
    guint cid;

    /* If cancelled, set result error */
    if (detailed_connect_context_set_error_if_cancelled (ctx, result_error))
        return FALSE;

    ctx->max_cid = 0;

    /* Some Android phones don't support querying existing PDP contexts,
     * but will accept setting the APN.  So if CGDCONT? isn't supported,
     * just ignore that error and hope for the best. (bgo #637327)
     */
    if (g_error_matches (error,
                         MM_MOBILE_EQUIPMENT_ERROR,
                         MM_MOBILE_EQUIPMENT_ERROR_NOT_SUPPORTED)) {
        mm_dbg ("Querying PDP context list is unsupported");
        return FALSE;
    }

    pdp_list = mm_3gpp_parse_pdp_query_response (response, &inner_error);
    if (!pdp_list) {
        /* No predefined PDP contexts found */
        mm_dbg ("No PDP contexts found");
        return FALSE;
    }

    cid = 0;
    mm_dbg ("Found '%u' PDP contexts", g_list_length (pdp_list));
    for (l = pdp_list; l; l = g_list_next (l)) {
        MM3gppPdpContext *pdp = l->data;

        mm_dbg ("  PDP context [cid=%u] [type='%s'] [apn='%s']",
                pdp->cid,
                pdp->pdp_type ? pdp->pdp_type : "",
                pdp->apn ? pdp->apn : "");
        if (g_str_equal (pdp->pdp_type, "IP")) {
            /* PDP with no APN set? we may use that one if not exact match found */
            if (!pdp->apn || !pdp->apn[0]) {
                mm_dbg ("Found PDP context with CID %u and no APN",
                        pdp->cid);
                cid = pdp->cid;
            } else if (g_str_equal (pdp->apn, ctx->self->priv->apn)) {
                /* Found a PDP context with the same CID, we'll use it. */
                mm_dbg ("Found PDP context with CID %u for APN '%s'",
                        pdp->cid, pdp->apn);
                cid = pdp->cid;
                /* In this case, stop searching */
                break;
            }
        }

        if (ctx->max_cid < pdp->cid)
            ctx->max_cid = pdp->cid;
    }
    mm_3gpp_pdp_context_list_free (pdp_list);

    if (cid > 0) {
        *result = g_variant_new_uint32 (cid);
        return TRUE;
    }

    return FALSE;
}

static const MMBaseModemAtCommand find_cid_sequence[] = {
    { "+CGDCONT?",  3, FALSE, (MMBaseModemAtResponseProcessor)parse_pdp_list  },
    { "+CGDCONT=?", 3, TRUE,  (MMBaseModemAtResponseProcessor)parse_cid_range },
    { NULL }
};

static void
connect_3gpp (MMBroadbandBearer *self,
              MMBroadbandModem *modem,
              MMAtSerialPort *primary,
              MMAtSerialPort *secondary, /* unused by us */
              MMPort *data,
              GCancellable *cancellable,
              GAsyncReadyCallback callback,
              gpointer user_data)
{
    DetailedConnectContext *ctx;

    ctx = detailed_connect_context_new (self,
                                        modem,
                                        primary,
                                        data,
                                        cancellable,
                                        callback,
                                        user_data);

    mm_dbg ("Looking for best CID...");
    mm_base_modem_at_sequence_in_port (
        ctx->modem,
        ctx->primary,
        find_cid_sequence,
        ctx, /* also passed as response processor context */
        NULL, /* response_processor_context_free */
        NULL, /* cancellable */
        (GAsyncReadyCallback)find_cid_ready,
        ctx);
}

/*****************************************************************************/
/* CONNECT */

typedef struct {
    MMPort *data;
    MMCommonBearerIpConfig *ipv4_config;
    MMCommonBearerIpConfig *ipv6_config;
} ConnectResult;

static void
connect_result_free (ConnectResult *result)
{
    if (result->ipv4_config)
        g_object_unref (result->ipv4_config);
    if (result->ipv6_config)
        g_object_unref (result->ipv6_config);
    g_object_unref (result->data);
    g_free (result);
}

typedef struct {
    MMBroadbandBearer *self;
    GSimpleAsyncResult *result;
    MMPort *data;
} ConnectContext;

static void
connect_context_complete_and_free (ConnectContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->data);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
connect_finish (MMBearer *self,
                GAsyncResult *res,
                MMPort **data,
                MMCommonBearerIpConfig **ipv4_config,
                MMCommonBearerIpConfig **ipv6_config,
                GError **error)
{
    ConnectResult *result;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return FALSE;

    result = (ConnectResult *) g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
    *data = MM_PORT (g_object_ref (result->data));
    *ipv4_config = (result->ipv4_config ? g_object_ref (result->ipv4_config) : NULL);
    *ipv6_config = (result->ipv6_config ? g_object_ref (result->ipv6_config) : NULL);

    return TRUE;
}

static void
connect_succeeded (ConnectContext *ctx,
                   MMCommonBearerIpConfig *ipv4_config,
                   MMCommonBearerIpConfig *ipv6_config)
{
    ConnectResult *result;

    /* Port is connected; update the state */
    mm_port_set_connected (ctx->data, TRUE);

    /* Build result */
    result = g_new0 (ConnectResult, 1);
    result->data = g_object_ref (ctx->data);
    result->ipv4_config = ipv4_config;
    result->ipv6_config = ipv6_config;

    /* Set operation result */
    g_simple_async_result_set_op_res_gpointer (ctx->result,
                                               result,
                                               (GDestroyNotify)connect_result_free);

    connect_context_complete_and_free (ctx);
}

static void
connect_failed (ConnectContext *ctx,
                GError *error)
{
    /* On errors, close the data port */
    if (MM_IS_AT_SERIAL_PORT (ctx->data))
        mm_serial_port_close (MM_SERIAL_PORT (ctx->data));

    g_simple_async_result_take_error (ctx->result, error);
    connect_context_complete_and_free (ctx);
}

static void
connect_cdma_ready (MMBroadbandBearer *self,
                    GAsyncResult *res,
                    ConnectContext *ctx)
{
    GError *error = NULL;
    MMCommonBearerIpConfig *ipv4_config = NULL;
    MMCommonBearerIpConfig *ipv6_config = NULL;

    if (!MM_BROADBAND_BEARER_GET_CLASS (self)->connect_cdma_finish (self,
                                                                    res,
                                                                    &ipv4_config,
                                                                    &ipv6_config,
                                                                    &error))
        connect_failed (ctx, error);
    else
        connect_succeeded (ctx, ipv4_config, ipv6_config);
}

static void
connect_3gpp_ready (MMBroadbandBearer *self,
                    GAsyncResult *res,
                    ConnectContext *ctx)
{
    GError *error = NULL;
    MMCommonBearerIpConfig *ipv4_config = NULL;
    MMCommonBearerIpConfig *ipv6_config = NULL;

    if (!MM_BROADBAND_BEARER_GET_CLASS (self)->connect_3gpp_finish (self,
                                                                    res,
                                                                    &ipv4_config,
                                                                    &ipv6_config,
                                                                    &error))
        connect_failed (ctx, error);
    else
        connect_succeeded (ctx, ipv4_config, ipv6_config);
}

static void
connect (MMBearer *self,
         GCancellable *cancellable,
         GAsyncReadyCallback callback,
         gpointer user_data)
{
    MMBaseModem *modem = NULL;
    MMAtSerialPort *primary;
    MMPort *data;
    ConnectContext *ctx;

    /* Don't try to connect if already connected */
    if (MM_BROADBAND_BEARER (self)->priv->port) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Couldn't connect: this bearer is already connected");
        return;
    }

    /* Get the owner modem object */
    g_object_get (self,
                  MM_BEARER_MODEM, &modem,
                  NULL);
    g_assert (modem != NULL);

    /* We will launch the ATD call in the primary port */
    primary = mm_base_modem_get_port_primary (modem);
    if (mm_port_get_connected (MM_PORT (primary))) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Couldn't connect: primary AT port is already connected");
        g_object_unref (modem);
        return;
    }

    /* Look for best data port, NULL if none available. */
    data = mm_base_modem_get_best_data_port (modem);
    if (!data) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_CONNECTED,
            "Couldn't connect: all available data ports already connected");
        g_object_unref (modem);
        return;
    }

    /* If data port is AT, we need to ensure it's open during the whole
     * connection. For the case where the primary port is used as data port,
     * which is actually always right now, this is already ensured because the
     * primary port is kept open as long as the modem is enabled, but anyway
     * there's no real problem in keeping an open count here as well. */
    if (MM_IS_AT_SERIAL_PORT (data)) {
        GError *error = NULL;

        if (!mm_serial_port_open (MM_SERIAL_PORT (data), &error)) {
            g_prefix_error (&error, "Couldn't connect: cannot keep data port open.");
            g_simple_async_report_take_gerror_in_idle (
                G_OBJECT (self),
                callback,
                user_data,
                error);
            g_object_unref (modem);
            return;
        }
    }

    /* In this context, we only keep the stuff we'll need later */
    ctx = g_new0 (ConnectContext, 1);
    ctx->self = g_object_ref (self);
    ctx->data = g_object_ref (data);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             connect);

    /* If the modem has 3GPP capabilities, launch 3GPP-based connection */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (modem))) {
        /* Launch connection if allowed */
        if (MM_BROADBAND_BEARER (self)->priv->reason_3gpp == CONNECTION_FORBIDDEN_REASON_NONE) {
            MM_BROADBAND_BEARER_GET_CLASS (self)->connect_3gpp (
                MM_BROADBAND_BEARER (self),
                MM_BROADBAND_MODEM (modem),
                primary,
                mm_base_modem_get_port_secondary (modem),
                data,
                cancellable,
                (GAsyncReadyCallback) connect_3gpp_ready,
                ctx);
            g_object_unref (modem);
            return;
        }

        mm_dbg ("Not allowed to connect bearer in 3GPP network: '%s'",
                connection_forbidden_reason_str[MM_BROADBAND_BEARER (self)->priv->reason_3gpp]);
    }

    /* Otherwise, launch CDMA-specific connection */
    if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (modem))) {
        /* Launch connection if allowed */
        if (MM_BROADBAND_BEARER (self)->priv->reason_cdma == CONNECTION_FORBIDDEN_REASON_NONE) {
            MM_BROADBAND_BEARER_GET_CLASS (self)->connect_cdma (
                MM_BROADBAND_BEARER (self),
                MM_BROADBAND_MODEM (modem),
                primary,
                mm_base_modem_get_port_secondary (modem),
                data,
                cancellable,
                (GAsyncReadyCallback) connect_cdma_ready,
                ctx);
            g_object_unref (modem);
            return;
        }
        mm_dbg ("Not allowed to connect bearer in CDMA network: '%s'",
                connection_forbidden_reason_str[MM_BROADBAND_BEARER (self)->priv->reason_cdma]);
    }

    g_simple_async_result_set_error (ctx->result,
                                     MM_CORE_ERROR,
                                     MM_CORE_ERROR_UNAUTHORIZED,
                                     "Not allowed to connect bearer");
    connect_context_complete_and_free (ctx);
}

/*****************************************************************************/
/* DISCONNECT (more or less the same for CDMA and 3GPP) */

typedef struct {
    MMBroadbandBearer *self;
    MMBaseModem *modem;
    MMAtSerialPort *primary;
    MMAtSerialPort *secondary;
    MMPort *data;
    GSimpleAsyncResult *result;
    GError *error;

    gboolean cgact_needed;
    gchar *cgact_command;
    gboolean cgact_sent;
} DisconnectContext;

static void
disconnect_context_complete_and_free (DisconnectContext *ctx)
{
    if (ctx->error) {
        g_simple_async_result_take_error (ctx->result, ctx->error);
    } else {
        /* If properly disconnected, close the data port */
        if (MM_IS_AT_SERIAL_PORT (ctx->data))
            mm_serial_port_close (MM_SERIAL_PORT (ctx->data));

        /* Port is disconnected; update the state */
        mm_port_set_connected (ctx->data, FALSE);
        mm_gdbus_bearer_set_connected (MM_GDBUS_BEARER (ctx->self), FALSE);
        mm_gdbus_bearer_set_interface (MM_GDBUS_BEARER (ctx->self), NULL);
        mm_gdbus_bearer_set_ip4_config (MM_GDBUS_BEARER (ctx->self), NULL);
        mm_gdbus_bearer_set_ip6_config (MM_GDBUS_BEARER (ctx->self), NULL);
        /* Clear data port and CID */
        if (ctx->self->priv->cid)
            ctx->self->priv->cid = 0;
        g_clear_object (&ctx->self->priv->port);

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    }

    g_simple_async_result_complete_in_idle (ctx->result);

    g_object_unref (ctx->data);
    g_object_unref (ctx->primary);
    if (ctx->secondary)
        g_object_unref (ctx->secondary);
    g_object_unref (ctx->self);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->result);
    g_free (ctx->cgact_command);
    g_free (ctx);
}

static gboolean
disconnect_finish (MMBearer *self,
                   GAsyncResult *res,
                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
cgact_primary_ready (MMBaseModem *modem,
                     GAsyncResult *res,
                     DisconnectContext *ctx)
{
    /* Ignore errors for now */
    mm_base_modem_at_command_finish (MM_BASE_MODEM (modem), res, NULL);

    disconnect_context_complete_and_free (ctx);
}

static void
primary_flash_ready (MMSerialPort *port,
                     GError *error,
                     DisconnectContext *ctx)
{
    if (error) {
        /* Ignore "NO CARRIER" response when modem disconnects and any flash
         * failures we might encounter. Other errors are hard errors.
         */
        if (!g_error_matches (error,
                              MM_CONNECTION_ERROR,
                              MM_CONNECTION_ERROR_NO_CARRIER) &&
            !g_error_matches (error,
                              MM_SERIAL_ERROR,
                              MM_SERIAL_ERROR_FLASH_FAILED)) {
            /* Fatal */
            ctx->error = g_error_copy (error);
            disconnect_context_complete_and_free (ctx);
            return;
        }

        mm_dbg ("Port flashing failed (not fatal): %s", error->message);
    }

    /* Don't bother doing the CGACT again if it was done on a secondary port
     * or if not needed */
    if (!ctx->cgact_needed ||
        !ctx->cgact_sent) {
        disconnect_context_complete_and_free (ctx);
        return;
    }

    mm_base_modem_at_command_in_port (
        ctx->modem,
        ctx->primary,
        ctx->cgact_command,
        3,
        FALSE,
        NULL, /* cancellable */
        (GAsyncReadyCallback)cgact_primary_ready,
        ctx);
}

static void
cgact_secondary_ready (MMBaseModem *modem,
                       GAsyncResult *res,
                       DisconnectContext *ctx)
{
    GError *error = NULL;

    mm_base_modem_at_command_finish (MM_BASE_MODEM (modem), res, &error);
    if (!error)
        ctx->cgact_sent = TRUE;
    else
        g_error_free (error);

    mm_serial_port_flash (MM_SERIAL_PORT (ctx->primary),
                          1000,
                          TRUE,
                          (MMSerialFlashFn)primary_flash_ready,
                          ctx);
}

static void
disconnect (MMBearer *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    DisconnectContext *ctx;
    MMBaseModem *modem = NULL;

    if (!MM_BROADBAND_BEARER (self)->priv->port) {
        g_simple_async_report_error_in_idle (
            G_OBJECT (self),
            callback,
            user_data,
            MM_CORE_ERROR,
            MM_CORE_ERROR_FAILED,
            "Couldn't disconnect: this bearer is not connected");
        return;
    }

    g_object_get (self,
                  MM_BEARER_MODEM, &modem,
                  NULL);
    g_assert (modem != NULL);

    ctx = g_new0 (DisconnectContext, 1);
    ctx->data = g_object_ref (MM_BROADBAND_BEARER (self)->priv->port);
    ctx->primary = g_object_ref (mm_base_modem_get_port_primary (modem));
    ctx->secondary = mm_base_modem_get_port_secondary (modem);
    if (ctx->secondary)
        g_object_ref (ctx->secondary);
    ctx->self = g_object_ref (self);
    ctx->modem = modem;
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disconnect);

    /* If the modem has 3GPP capabilities, send CGACT to disable contexts */
    if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (modem))) {
        ctx->cgact_needed = TRUE;

        /* If no specific CID was used, disable all PDP contexts */
        ctx->cgact_command =
            (MM_BROADBAND_BEARER (self)->priv->cid >= 0 ?
             g_strdup_printf ("+CGACT=0,%d", MM_BROADBAND_BEARER (self)->priv->cid) :
             g_strdup_printf ("+CGACT=0"));

        /* If the primary port is connected (with PPP) then try sending the PDP
         * context deactivation on the secondary port because not all modems will
         * respond to flashing (since either the modem or the kernel's serial
         * driver doesn't support it).
         */
        if (ctx->secondary &&
            mm_port_get_connected (MM_PORT (ctx->primary))) {
            mm_base_modem_at_command_in_port (
                ctx->modem,
                ctx->secondary,
                ctx->cgact_command,
                3,
                FALSE,
                NULL, /* cancellable */
                (GAsyncReadyCallback)cgact_secondary_ready,
                ctx);
            return;
        }
    }

    /* If CGACT not needed, or if no secondary port, go on to flash the primary port */
    mm_serial_port_flash (MM_SERIAL_PORT (ctx->primary),
                          1000,
                          TRUE,
                          (MMSerialFlashFn)primary_flash_ready,
                          ctx);
}

/*****************************************************************************/

typedef struct _InitAsyncContext InitAsyncContext;
static void interface_initialization_step (InitAsyncContext *ctx);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_CDMA_RM_PROTOCOL,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitAsyncContext {
    MMBroadbandBearer *self;
    GSimpleAsyncResult *result;
    GCancellable *cancellable;
    MMBaseModem *modem;
    InitializationStep step;
    MMAtSerialPort *port;
};

static void
init_async_context_free (InitAsyncContext *ctx,
                         gboolean close_port)
{
    if (close_port)
        mm_serial_port_close (MM_SERIAL_PORT (ctx->port));
    g_object_unref (ctx->self);
    g_object_unref (ctx->modem);
    g_object_unref (ctx->result);
    if (ctx->cancellable)
        g_object_unref (ctx->cancellable);
    g_free (ctx);
}

MMBearer *
mm_broadband_bearer_new_finish (GAsyncResult *res,
                                GError **error)
{
    GObject *bearer;
    GObject *source;

    source = g_async_result_get_source_object (res);
    bearer = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, error);
    g_object_unref (source);

    if (!bearer)
        return NULL;

    /* Only export valid bearers */
    mm_bearer_export (MM_BEARER (bearer));

    return MM_BEARER (bearer);
}

static gboolean
initable_init_finish (GAsyncInitable  *initable,
                      GAsyncResult    *result,
                      GError         **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error);
}

static void
crm_range_ready (MMBaseModem *modem,
                 GAsyncResult *res,
                 InitAsyncContext *ctx)
{
    GError *error = NULL;
    const gchar *response;

    response = mm_base_modem_at_command_finish (modem, res, &error);
    if (error) {
        /* We should possibly take this error as fatal. If we were told to use a
         * specific Rm protocol, we must be able to check if it is supported. */
        g_simple_async_result_take_error (ctx->result, error);
    } else {
        MMModemCdmaRmProtocol min = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;
        MMModemCdmaRmProtocol max = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;

        if (mm_cdma_parse_crm_range_response (response,
                                              &min, &max,
                                              &error)) {
            /* Check if value within the range */
            if (ctx->self->priv->rm_protocol >= min &&
                ctx->self->priv->rm_protocol <= max) {
                /* Fine, go on with next step */
                ctx->step++;
                interface_initialization_step (ctx);
            }

            g_assert (error == NULL);
            error = g_error_new (MM_CORE_ERROR,
                                 MM_CORE_ERROR_FAILED,
                                 "Requested RM protocol '%s' is not supported",
                                 mm_modem_cdma_rm_protocol_get_string (
                                     ctx->self->priv->rm_protocol));
        }

        /* Failed, set as fatal as well */
        g_simple_async_result_take_error (ctx->result, error);
    }

    g_simple_async_result_complete (ctx->result);
    init_async_context_free (ctx, TRUE);
}

static void
modem_3gpp_registration_state_changed (MMBroadbandModem *modem,
                                       GParamSpec *pspec,
                                       MMBroadbandBearer *self)
{
    MMModem3gppRegistrationState state = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;

    g_object_get (modem,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &state,
                  NULL);

    switch (state) {
    case MM_MODEM_3GPP_REGISTRATION_STATE_IDLE:
    case MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING:
    case MM_MODEM_3GPP_REGISTRATION_STATE_DENIED:
    case MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN:
        mm_dbg ("Bearer not allowed to connect, not registered");
        self->priv->reason_3gpp = CONNECTION_FORBIDDEN_REASON_UNREGISTERED;
        break;
    case MM_MODEM_3GPP_REGISTRATION_STATE_HOME:
        mm_dbg ("Bearer allowed to connect, registered in home network");
        self->priv->reason_3gpp = CONNECTION_FORBIDDEN_REASON_NONE;
        break;
    case MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING:
        if (self->priv->allow_roaming) {
            mm_dbg ("Bearer allowed to connect, registered in roaming network");
            self->priv->reason_3gpp = CONNECTION_FORBIDDEN_REASON_NONE;
        } else {
            mm_dbg ("Bearer not allowed to connect, registered in roaming network");
            self->priv->reason_3gpp = CONNECTION_FORBIDDEN_REASON_ROAMING;
        }
        break;
    }

    /* close connection if we're connected in 3GPP */
    if (self->priv->reason_3gpp != CONNECTION_FORBIDDEN_REASON_NONE &&
        self->priv->connection_type == CONNECTION_TYPE_3GPP)
        mm_bearer_disconnect_force (MM_BEARER (self));
}

static void
modem_cdma_registration_state_changed (MMBroadbandModem *modem,
                                       GParamSpec *pspec,
                                       MMBroadbandBearer *self)
{
    MMModemCdmaRegistrationState cdma1x_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
    MMModemCdmaRegistrationState evdo_state = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;

    g_object_get (modem,
                  MM_IFACE_MODEM_CDMA_CDMA1X_REGISTRATION_STATE, &cdma1x_state,
                  MM_IFACE_MODEM_CDMA_EVDO_REGISTRATION_STATE, &evdo_state,
                  NULL);

    if (cdma1x_state == MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING ||
        evdo_state == MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING) {
        if (self->priv->allow_roaming) {
            mm_dbg ("Bearer allowed to connect, registered in roaming network");
            self->priv->reason_cdma = CONNECTION_FORBIDDEN_REASON_NONE;
        } else {
            mm_dbg ("Bearer not allowed to connect, registered in roaming network");
            self->priv->reason_cdma = CONNECTION_FORBIDDEN_REASON_ROAMING;
        }
    } else if (cdma1x_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN ||
               evdo_state != MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN) {
        mm_dbg ("Bearer allowed to connect, registered in home network");
        self->priv->reason_cdma = CONNECTION_FORBIDDEN_REASON_NONE;
    } else {
        mm_dbg ("Bearer not allowed to connect, not registered");
        self->priv->reason_cdma = CONNECTION_FORBIDDEN_REASON_UNREGISTERED;
    }

    /* close connection if we're connected in CDMA */
    if (self->priv->reason_cdma != CONNECTION_FORBIDDEN_REASON_NONE &&
        self->priv->connection_type == CONNECTION_TYPE_CDMA)
        mm_bearer_disconnect_force (MM_BEARER (self));
}

static void
interface_initialization_step (InitAsyncContext *ctx)
{
    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_CDMA_RM_PROTOCOL:
        /* If a specific RM protocol is given, we need to check whether it is
         * supported. */
        if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (ctx->modem)) &&
            ctx->self->priv->rm_protocol != MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN) {
            mm_base_modem_at_command_in_port (
                ctx->modem,
                ctx->port,
                "+CRM=?",
                3,
                TRUE, /* getting range, so reply can be cached */
                NULL, /* cancellable */
                (GAsyncReadyCallback)crm_range_ready,
                ctx);
            return;
        }

        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:

        if (mm_iface_modem_is_3gpp (MM_IFACE_MODEM (ctx->modem))) {
            ctx->self->priv->id_3gpp_registration_change =
                g_signal_connect (ctx->modem,
                                  "notify::" MM_IFACE_MODEM_3GPP_REGISTRATION_STATE,
                                  G_CALLBACK (modem_3gpp_registration_state_changed),
                                  ctx->self);
            modem_3gpp_registration_state_changed (MM_BROADBAND_MODEM (ctx->modem), NULL, ctx->self);
        }

        if (mm_iface_modem_is_cdma (MM_IFACE_MODEM (ctx->modem))) {
            ctx->self->priv->id_cdma1x_registration_change =
                g_signal_connect (ctx->modem,
                                  "notify::" MM_IFACE_MODEM_CDMA_CDMA1X_REGISTRATION_STATE,
                                  G_CALLBACK (modem_cdma_registration_state_changed),
                                  ctx->self);
            ctx->self->priv->id_evdo_registration_change =
                g_signal_connect (ctx->modem,
                                  "notify::" MM_IFACE_MODEM_CDMA_EVDO_REGISTRATION_STATE,
                                  G_CALLBACK (modem_cdma_registration_state_changed),
                              ctx->self);
            modem_cdma_registration_state_changed (MM_BROADBAND_MODEM (ctx->modem), NULL, ctx->self);
        }

        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        g_simple_async_result_complete_in_idle (ctx->result);
        init_async_context_free (ctx, TRUE);
        return;
    }

    g_assert_not_reached ();
}

static void
initable_init_async (GAsyncInitable *initable,
                     int io_priority,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    InitAsyncContext *ctx;
    GError *error = NULL;

    ctx = g_new0 (InitAsyncContext, 1);
    ctx->self = g_object_ref (initable);
    ctx->result = g_simple_async_result_new (G_OBJECT (initable),
                                             callback,
                                             user_data,
                                             initable_init_async);
    ctx->cancellable = (cancellable ?
                        g_object_ref (cancellable) :
                        NULL);

    g_object_get (initable,
                  MM_BEARER_MODEM, &ctx->modem,
                  NULL);

    ctx->port = mm_base_modem_get_port_primary (ctx->modem);
    if (!mm_serial_port_open (MM_SERIAL_PORT (ctx->port), &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        g_simple_async_result_complete_in_idle (ctx->result);
        init_async_context_free (ctx, FALSE);
        return;
    }

    interface_initialization_step (ctx);
}

void
mm_broadband_bearer_new (MMBroadbandModem *modem,
                         MMCommonBearerProperties *properties,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    g_async_initable_new_async (
        MM_TYPE_BROADBAND_BEARER,
        G_PRIORITY_DEFAULT,
        cancellable,
        callback,
        user_data,
        MM_BEARER_MODEM,                      modem,
        MM_BROADBAND_BEARER_3GPP_APN,         mm_common_bearer_properties_get_apn (properties),
        MM_BROADBAND_BEARER_CDMA_NUMBER,      mm_common_bearer_properties_get_number (properties),
        MM_BROADBAND_BEARER_CDMA_RM_PROTOCOL, mm_common_bearer_properties_get_rm_protocol (properties),
        MM_BROADBAND_BEARER_IP_TYPE,          mm_common_bearer_properties_get_ip_type (properties),
        MM_BROADBAND_BEARER_ALLOW_ROAMING,    mm_common_bearer_properties_get_allow_roaming (properties),
        NULL);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMBroadbandBearer *self = MM_BROADBAND_BEARER (object);

    switch (prop_id) {
    case PROP_3GPP_APN:
        g_free (self->priv->apn);
        self->priv->apn = g_value_dup_string (value);
        break;
    case PROP_CDMA_NUMBER:
        g_free (self->priv->number);
        self->priv->number = g_value_dup_string (value);
        break;
    case PROP_CDMA_RM_PROTOCOL:
        self->priv->rm_protocol = g_value_get_enum (value);
        break;
    case PROP_IP_TYPE:
        g_free (self->priv->ip_type);
        self->priv->ip_type = g_value_dup_string (value);
        break;
    case PROP_ALLOW_ROAMING:
        self->priv->allow_roaming = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
    MMBroadbandBearer *self = MM_BROADBAND_BEARER (object);

    switch (prop_id) {
    case PROP_3GPP_APN:
        g_value_set_string (value, self->priv->apn);
        break;
    case PROP_CDMA_NUMBER:
        g_value_set_string (value, self->priv->number);
        break;
    case PROP_CDMA_RM_PROTOCOL:
        g_value_set_enum (value, self->priv->rm_protocol);
        break;
    case PROP_IP_TYPE:
        g_value_set_string (value, self->priv->ip_type);
        break;
    case PROP_ALLOW_ROAMING:
        g_value_set_boolean (value, self->priv->allow_roaming);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_broadband_bearer_init (MMBroadbandBearer *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_BEARER,
                                              MMBroadbandBearerPrivate);

    /* Set defaults */
    self->priv->connection_type = CONNECTION_TYPE_NONE;
    self->priv->allow_roaming = TRUE;
    self->priv->rm_protocol = MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN;
    self->priv->reason_3gpp = CONNECTION_FORBIDDEN_REASON_NONE;
    self->priv->reason_cdma = CONNECTION_FORBIDDEN_REASON_NONE;
}

static void
dispose (GObject *object)
{
    MMBroadbandBearer *self = MM_BROADBAND_BEARER (object);
    MMBroadbandModem *modem = NULL;

    g_object_get (self,
                  MM_BEARER_MODEM, &modem,
                  NULL);

    /* We will disconnect the signals before calling parent's dispose, so that
     * we can get the 'modem' object we need to perform the disconnection */
    if (self->priv->id_3gpp_registration_change) {
        g_signal_handler_disconnect (modem, self->priv->id_3gpp_registration_change);
        self->priv->id_3gpp_registration_change = 0;
    }
    if (self->priv->id_cdma1x_registration_change) {
        g_signal_handler_disconnect (modem, self->priv->id_cdma1x_registration_change);
        self->priv->id_cdma1x_registration_change = 0;
    }
    if (self->priv->id_evdo_registration_change) {
        g_signal_handler_disconnect (modem, self->priv->id_evdo_registration_change);
        self->priv->id_evdo_registration_change = 0;
    }

    g_object_unref (modem);

    G_OBJECT_CLASS (mm_broadband_bearer_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
    MMBroadbandBearer *self = MM_BROADBAND_BEARER (object);

    g_free (self->priv->apn);
    g_free (self->priv->ip_type);

    G_OBJECT_CLASS (mm_broadband_bearer_parent_class)->finalize (object);
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
    iface->init_async = initable_init_async;
    iface->init_finish = initable_init_finish;
}

static void
mm_broadband_bearer_class_init (MMBroadbandBearerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBearerClass *bearer_class = MM_BEARER_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandBearerPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    bearer_class->connect = connect;
    bearer_class->connect_finish = connect_finish;
    bearer_class->disconnect = disconnect;
    bearer_class->disconnect_finish = disconnect_finish;

    klass->connect_3gpp = connect_3gpp;
    klass->connect_3gpp_finish = detailed_connect_finish;
    klass->connect_cdma = connect_cdma;
    klass->connect_cdma_finish = detailed_connect_finish;

    properties[PROP_3GPP_APN] =
        g_param_spec_string (MM_BROADBAND_BEARER_3GPP_APN,
                             "3GPP APN",
                             "Access Point Name to use in the connection",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_3GPP_APN, properties[PROP_3GPP_APN]);

    properties[PROP_CDMA_NUMBER] =
        g_param_spec_string (MM_BROADBAND_BEARER_CDMA_NUMBER,
                             "Number to dial",
                             "Number to dial when launching the CDMA connection",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_CDMA_NUMBER, properties[PROP_CDMA_NUMBER]);

    properties[PROP_CDMA_RM_PROTOCOL] =
        g_param_spec_enum (MM_BROADBAND_BEARER_CDMA_RM_PROTOCOL,
                           "Rm Protocol",
                           "Protocol to use in the CDMA Rm interface",
                           MM_TYPE_MODEM_CDMA_RM_PROTOCOL,
                           MM_MODEM_CDMA_RM_PROTOCOL_UNKNOWN,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_CDMA_RM_PROTOCOL, properties[PROP_CDMA_RM_PROTOCOL]);

    properties[PROP_IP_TYPE] =
        g_param_spec_string (MM_BROADBAND_BEARER_IP_TYPE,
                             "IP type",
                             "IP setup to use in the connection",
                             NULL,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_IP_TYPE, properties[PROP_IP_TYPE]);

    properties[PROP_ALLOW_ROAMING] =
        g_param_spec_boolean (MM_BROADBAND_BEARER_ALLOW_ROAMING,
                              "Allow roaming",
                              "Whether connections are allowed when roaming",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_ALLOW_ROAMING, properties[PROP_ALLOW_ROAMING]);
}
