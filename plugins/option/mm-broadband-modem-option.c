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
 * Copyright (C) 2012 Aleksander Morgado <aleksander@gnu.org>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-modem-helpers.h"
#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-base-modem-at.h"
#include "mm-broadband-modem-option.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemOption, mm_broadband_modem_option, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init));

struct _MMBroadbandModemOptionPrivate {
    /* Regex for access-technology related notifications */
    GRegex *_ossysi_regex;
    GRegex *_octi_regex;
    GRegex *_ouwcti_regex;

    /* Regex for signal quality related notifications */
    GRegex *_osigq_regex;

    guint after_power_up_wait_id;
};

/*****************************************************************************/
/* Load access technologies (Modem interface) */

typedef enum {
    ACCESS_TECHNOLOGIES_STEP_FIRST,
    ACCESS_TECHNOLOGIES_STEP_OSSYS,
    ACCESS_TECHNOLOGIES_STEP_OCTI,
    ACCESS_TECHNOLOGIES_STEP_OWCTI,
    ACCESS_TECHNOLOGIES_STEP_LAST
} AccessTechnologiesStep;

typedef struct {
    MMBroadbandModemOption *self;
    GSimpleAsyncResult *result;
    MMModemAccessTechnology access_technology;
    gboolean check_2g;
    gboolean check_3g;
    AccessTechnologiesStep step;
} AccessTechnologiesContext;

static void load_access_technologies_step (AccessTechnologiesContext *ctx);

static void
access_technologies_context_complete_and_free (AccessTechnologiesContext *ctx)
{
    g_simple_async_result_complete (ctx->result);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gboolean
load_access_technologies_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 MMModemAccessTechnology *access_technologies,
                                 guint *mask,
                                 GError **error)
{
    const gchar *p;
    const gchar *response;

    response = mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, error);
    if (!response)
        return FALSE;

    p = mm_strip_tag (response, "*CNTI:");
    p = strchr (p, ',');
    if (p) {
        /* We are reporting ALL 3GPP access technologies here */
        *access_technologies = mm_3gpp_string_to_access_tech (p + 1);
        *mask = MM_IFACE_MODEM_3GPP_ALL_ACCESS_TECHNOLOGIES_MASK;
        return TRUE;
    }

    g_set_error (error,
                 MM_CORE_ERROR,
                 MM_CORE_ERROR_FAILED,
                 "Couldn't parse access technologies result: '%s'",
                 response);
    return FALSE;
}

static gboolean
ossys_to_mm (gchar ossys,
             MMModemAccessTechnology *access_technology)
{
    if (ossys == '0') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
        return TRUE;
    }

    if (ossys == '2') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
        return TRUE;
    }

    if (ossys == '3') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
        return TRUE;
    }

    return FALSE;
}

static gboolean
parse_ossys_response (const gchar *response,
                      MMModemAccessTechnology *access_technology)
{
    MMModemAccessTechnology current = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    const gchar *p;
    GRegex *r;
    GMatchInfo *match_info;
    gchar *str;
    gboolean success = FALSE;

    p = mm_strip_tag (response, "_OSSYS:");
    r = g_regex_new ("(\\d),(\\d)", G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r != NULL);

    g_regex_match (r, p, 0, &match_info);
    if (g_match_info_matches (match_info)) {
        str = g_match_info_fetch (match_info, 2);
        if (str && ossys_to_mm (str[0], &current)) {
            *access_technology = current;
            success = TRUE;
        }
        g_free (str);
    }
    g_match_info_free (match_info);
    g_regex_unref (r);

    return success;
}

static void
ossys_query_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   AccessTechnologiesContext *ctx)
{
    const gchar *response;

    /* If for some reason the OSSYS request failed, still try to check
     * explicit 2G/3G mode with OCTI and OWCTI; maybe we'll get something.
     */
    response = mm_base_modem_at_command_finish (self, res, NULL);
    /* Response is _OSSYS: <n>,<act> so we must skip the <n> */
    if (response &&
        parse_ossys_response (response, &ctx->access_technology)) {
        /* If the OSSYS response indicated a generic access tech type
         * then only check for more specific access tech of that type.
         */
        if (ctx->access_technology == MM_MODEM_ACCESS_TECHNOLOGY_GPRS)
            ctx->check_3g = FALSE;
        else if (ctx->access_technology == MM_MODEM_ACCESS_TECHNOLOGY_UMTS)
            ctx->check_2g = FALSE;
    }

    /* Go on to next step */
    ctx->step++;
    load_access_technologies_step (ctx);
}

static gboolean
octi_to_mm (gchar octi,
            MMModemAccessTechnology *access_technology)
{
    if (octi == '1') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_GSM;
        return TRUE;
    }

    if (octi == '2') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_GPRS;
        return TRUE;
    }

    if (octi == '3') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_EDGE;
        return TRUE;
    }

    return FALSE;
}

static gboolean
parse_octi_response (const gchar *response,
                     MMModemAccessTechnology *access_technology)
{
    MMModemAccessTechnology current = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    const gchar *p;
    GRegex *r;
    GMatchInfo *match_info;
    gchar *str;
    gboolean success = FALSE;

    p = mm_strip_tag (response, "_OCTI:");
    r = g_regex_new ("(\\d),(\\d)", G_REGEX_UNGREEDY, 0, NULL);
    g_assert (r != NULL);

    g_regex_match (r, p, 0, &match_info);
    if (g_match_info_matches (match_info)) {
        str = g_match_info_fetch (match_info, 2);
        if (str && octi_to_mm (str[0], &current)) {
            *access_technology = current;
            success = TRUE;
        }
        g_free (str);
    }
    g_match_info_free (match_info);
    g_regex_unref (r);

    return success;
}

static void
octi_query_ready (MMBaseModem *self,
                  GAsyncResult *res,
                  AccessTechnologiesContext *ctx)
{
    MMModemAccessTechnology octi = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    const gchar *response;

    response = mm_base_modem_at_command_finish (self, res, NULL);
    if (response &&
        parse_octi_response (response, &octi)) {
        /* If current tech is 2G or unknown then use the more specific
         * OCTI response.
         */
        if (ctx->access_technology < MM_MODEM_ACCESS_TECHNOLOGY_UMTS)
            ctx->access_technology = octi;
    }

    /* Go on to next step */
    ctx->step++;
    load_access_technologies_step (ctx);
}

static gboolean
owcti_to_mm (gchar owcti, MMModemAccessTechnology *access_technology)
{
    if (owcti == '1') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_UMTS;
        return TRUE;
    }

    if (owcti == '2') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_HSDPA;
        return TRUE;
    }

    if (owcti == '3') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_HSUPA;
        return TRUE;
    }

    if (owcti == '4') {
        *access_technology = MM_MODEM_ACCESS_TECHNOLOGY_HSPA;
        return TRUE;
    }

    return FALSE;
}

static gboolean
parse_owcti_response (const gchar *response,
                      MMModemAccessTechnology *access_technology)
{
    response = mm_strip_tag (response, "_OWCTI:");
    return owcti_to_mm (*response, access_technology);
}

static void
owcti_query_ready (MMBaseModem *self,
                   GAsyncResult *res,
                   AccessTechnologiesContext *ctx)
{
    MMModemAccessTechnology owcti = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;
    const gchar *response;

    response = mm_base_modem_at_command_finish (self, res, NULL);
    if (response &&
        parse_owcti_response (response, &owcti)) {
        ctx->access_technology = owcti;
    }

    /* Go on to next step */
    ctx->step++;
    load_access_technologies_step (ctx);
}

static void
load_access_technologies_step (AccessTechnologiesContext *ctx)
{
    switch (ctx->step) {
    case ACCESS_TECHNOLOGIES_STEP_FIRST:
        /* Go on to next step */
        ctx->step++;

    case ACCESS_TECHNOLOGIES_STEP_OSSYS:
        mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                                  "_OSSYS?",
                                  3,
                                  FALSE,
                                  (GAsyncReadyCallback)ossys_query_ready,
                                  ctx);
        break;

    case ACCESS_TECHNOLOGIES_STEP_OCTI:
        if (ctx->check_2g) {
            mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                                      "_OCTI?",
                                      3,
                                      FALSE,
                                      (GAsyncReadyCallback)octi_query_ready,
                                      ctx);
            return;
        }
        /* Go on to next step */
        ctx->step++;

    case ACCESS_TECHNOLOGIES_STEP_OWCTI:
        if (ctx->check_3g) {
            mm_base_modem_at_command (MM_BASE_MODEM (ctx->self),
                                      "_OWCTI?",
                                      3,
                                      FALSE,
                                      (GAsyncReadyCallback)owcti_query_ready,
                                      ctx);
            return;
        }
        /* Go on to next step */
        ctx->step++;

    case ACCESS_TECHNOLOGIES_STEP_LAST:
        /* All done, set result and complete */
        g_simple_async_result_set_op_res_gpointer (ctx->result,
                                                   GUINT_TO_POINTER (ctx->access_technology),
                                                   NULL);
        access_technologies_context_complete_and_free (ctx);
        break;
    }
}

static void
load_access_technologies (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    AccessTechnologiesContext *ctx;

    ctx = g_new (AccessTechnologiesContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             load_access_technologies);
    ctx->step = ACCESS_TECHNOLOGIES_STEP_FIRST;
    ctx->check_2g = TRUE;
    ctx->check_3g = TRUE;
    ctx->access_technology = MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN;

    load_access_technologies_step (ctx);
}

/*****************************************************************************/
/* After power up (Modem interface) */

static gboolean
modem_after_power_up_finish (MMIfaceModem *self,
                             GAsyncResult *res,
                             GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static gboolean
after_power_up_wait_cb (GSimpleAsyncResult *result)
{
    MMBroadbandModemOption *option;

    option = MM_BROADBAND_MODEM_OPTION (g_async_result_get_source_object (G_ASYNC_RESULT (result)));

    g_simple_async_result_set_op_res_gboolean (result, TRUE);
    g_simple_async_result_complete (result);
    g_object_unref (result);

    option->priv->after_power_up_wait_id = 0;
    g_object_unref (option);

    return FALSE;
}

static void
modem_after_power_up (MMIfaceModem *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    MMBroadbandModemOption *option = MM_BROADBAND_MODEM_OPTION (self);
    GSimpleAsyncResult *result;

    /* Some Option devices return OK on +CFUN=1 right away but need some time
     * to finish initialization.
     */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_after_power_up);
    g_warn_if_fail (option->priv->after_power_up_wait_id == 0);
    option->priv->after_power_up_wait_id =
        g_timeout_add_seconds (10,
                               (GSourceFunc)after_power_up_wait_cb,
                               result);
}

/*****************************************************************************/
/* Setup ports (Broadband modem class) */

static void
setup_ports (MMBroadbandModem *self)
{
    MMBroadbandModemOption *option = MM_BROADBAND_MODEM_OPTION (self);
    MMAtSerialPort *primary;

    /* Call parent's setup ports first always */
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_option_parent_class)->setup_ports (self);

    /* Now reset the unsolicited messages we'll handle, only in the primary port! */
    primary = mm_base_modem_peek_port_primary (MM_BASE_MODEM (self));
    mm_at_serial_port_add_unsolicited_msg_handler (primary,
                                                   option->priv->_ossysi_regex,
                                                   NULL, NULL, NULL);
    mm_at_serial_port_add_unsolicited_msg_handler (primary,
                                                   option->priv->_octi_regex,
                                                   NULL, NULL, NULL);
    mm_at_serial_port_add_unsolicited_msg_handler (primary,
                                                   option->priv->_ouwcti_regex,
                                                   NULL, NULL, NULL);
    mm_at_serial_port_add_unsolicited_msg_handler (primary,
                                                   option->priv->_osigq_regex,
                                                   NULL, NULL, NULL);
}

/*****************************************************************************/

MMBroadbandModemOption *
mm_broadband_modem_option_new (const gchar *device,
                              const gchar *driver,
                              const gchar *plugin,
                              guint16 vendor_id,
                              guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_OPTION,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
finalize (GObject *object)
{
    MMBroadbandModemOption *self = MM_BROADBAND_MODEM_OPTION (object);

    g_regex_unref (self->priv->_ossysi_regex);
    g_regex_unref (self->priv->_octi_regex);
    g_regex_unref (self->priv->_ouwcti_regex);
    g_regex_unref (self->priv->_osigq_regex);
}

static void
mm_broadband_modem_option_init (MMBroadbandModemOption *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_BROADBAND_MODEM_OPTION,
                                              MMBroadbandModemOptionPrivate);
    self->priv->after_power_up_wait_id = 0;

    /* Prepare regular expressions to setup */
    self->priv->_ossysi_regex = g_regex_new ("\\r\\n_OSSYSI:\\s*(\\d+)\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->_octi_regex = g_regex_new ("\\r\\n_OCTI:\\s*(\\d+)\\r\\n",
                                           G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->_ouwcti_regex = g_regex_new ("\\r\\n_OUWCTI:\\s*(\\d+)\\r\\n",
                                             G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
    self->priv->_osigq_regex = g_regex_new ("\\r\\n_OSIGQ:\\s*(\\d+),(\\d)\\r\\n",
                                            G_REGEX_RAW | G_REGEX_OPTIMIZE, 0, NULL);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    iface->modem_after_power_up = modem_after_power_up;
    iface->modem_after_power_up_finish = modem_after_power_up_finish;
    iface->load_access_technologies = load_access_technologies;
    iface->load_access_technologies_finish = load_access_technologies_finish;
}

static void
mm_broadband_modem_option_class_init (MMBroadbandModemOptionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMBroadbandModemOptionPrivate));

    object_class->finalize = finalize;
    broadband_modem_class->setup_ports = setup_ports;
}
