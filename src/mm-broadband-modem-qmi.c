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
 * Copyright (C) 2012 Google Inc.
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "mm-broadband-modem-qmi.h"

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-iface-modem.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemQmi, mm_broadband_modem_qmi, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init));

/*****************************************************************************/

static gboolean
ensure_qmi_client (MMBroadbandModemQmi *self,
                   QmiService service,
                   QmiClient **o_client,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    QmiClient *client;

    client = mm_qmi_port_peek_client (mm_base_modem_peek_port_qmi (MM_BASE_MODEM (self)), service);
    if (!client) {
        g_simple_async_report_error_in_idle (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             MM_CORE_ERROR,
                                             MM_CORE_ERROR_FAILED,
                                             "Couldn't peek client for service '%s'",
                                             qmi_service_get_string (service));
        return FALSE;
    }

    *o_client = client;
    return TRUE;
}

/*****************************************************************************/
/* Capabilities loading (Modem interface) */

static MMModemCapability
modem_load_current_capabilities_finish (MMIfaceModem *self,
                                        GAsyncResult *res,
                                        GError **error)
{
    MMModemCapability caps;
    gchar *caps_str;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return MM_MODEM_CAPABILITY_NONE;

    caps = ((MMModemCapability) GPOINTER_TO_UINT (
                g_simple_async_result_get_op_res_gpointer (
                    G_SIMPLE_ASYNC_RESULT (res))));
    caps_str = mm_modem_capability_build_string_from_mask (caps);
    mm_dbg ("loaded current capabilities: %s", caps_str);
    g_free (caps_str);
    return caps;
}

static MMModemCapability
qmi_network_to_modem_capability (QmiDmsRadioInterface network)
{
    switch (network) {
    case QMI_DMS_RADIO_INTERFACE_CDMA20001X:
        return MM_MODEM_CAPABILITY_CDMA_EVDO;
    case QMI_DMS_RADIO_INTERFACE_EVDO:
        return MM_MODEM_CAPABILITY_CDMA_EVDO;
    case QMI_DMS_RADIO_INTERFACE_GSM:
        return MM_MODEM_CAPABILITY_GSM_UMTS;
    case QMI_DMS_RADIO_INTERFACE_UMTS:
        return MM_MODEM_CAPABILITY_GSM_UMTS;
    case QMI_DMS_RADIO_INTERFACE_LTE:
        return MM_MODEM_CAPABILITY_LTE;
    default:
        mm_warn ("Unhandled QMI radio interface received (%u)",
                 (guint)network);
        return MM_MODEM_CAPABILITY_NONE;
    }
}

static void
dms_get_capabilities_ready (QmiClientDms *client,
                            GAsyncResult *res,
                            GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetCapabilitiesOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_capabilities_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_capabilities_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Capabilities: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        guint i;
        guint mask = MM_MODEM_CAPABILITY_NONE;
        GArray *radio_interface_list;

        qmi_message_dms_get_capabilities_output_get_info (
            output,
            NULL, /* info_max_tx_channel_rate */
            NULL, /* info_max_rx_channel_rate */
            NULL, /* info_data_service_capability */
            NULL, /* info_sim_capability */
            &radio_interface_list,
            NULL);

        for (i = 0; i < radio_interface_list->len; i++) {
            mask |= qmi_network_to_modem_capability (g_array_index (radio_interface_list,
                                                                    QmiDmsRadioInterface,
                                                                    i));
        }

        g_simple_async_result_set_op_res_gpointer (simple,
                                                   GUINT_TO_POINTER (mask),
                                                   NULL);
    }

    if (output)
        qmi_message_dms_get_capabilities_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_current_capabilities (MMIfaceModem *self,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_current_capabilities);

    mm_dbg ("loading current capabilities...");
    qmi_client_dms_get_capabilities (QMI_CLIENT_DMS (client),
                                     NULL,
                                     5,
                                     NULL,
                                     (GAsyncReadyCallback)dms_get_capabilities_ready,
                                     result);
}

/*****************************************************************************/
/* Manufacturer loading (Modem interface) */

static gchar *
modem_load_manufacturer_finish (MMIfaceModem *self,
                                GAsyncResult *res,
                                GError **error)
{
    gchar *manufacturer;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    manufacturer = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded manufacturer: %s", manufacturer);
    return manufacturer;
}

static void
dms_get_manufacturer_ready (QmiClientDms *client,
                            GAsyncResult *res,
                            GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetManufacturerOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_manufacturer_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_manufacturer_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Manufacturer: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        const gchar *str;

        qmi_message_dms_get_manufacturer_output_get_manufacturer (output, &str, NULL);
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_strdup (str),
                                                   (GDestroyNotify)g_free);
    }

    if (output)
        qmi_message_dms_get_manufacturer_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_manufacturer (MMIfaceModem *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_manufacturer);

    mm_dbg ("loading manufacturer...");
    qmi_client_dms_get_manufacturer (QMI_CLIENT_DMS (client),
                                     NULL,
                                     5,
                                     NULL,
                                     (GAsyncReadyCallback)dms_get_manufacturer_ready,
                                     result);
}

/*****************************************************************************/
/* Model loading (Modem interface) */

static gchar *
modem_load_model_finish (MMIfaceModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    gchar *model;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    model = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded model: %s", model);
    return model;
}

static void
dms_get_model_ready (QmiClientDms *client,
                     GAsyncResult *res,
                     GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetModelOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_model_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_model_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Model: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        const gchar *str;

        qmi_message_dms_get_model_output_get_model (output, &str, NULL);
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_strdup (str),
                                                   (GDestroyNotify)g_free);
    }

    if (output)
        qmi_message_dms_get_model_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_model (MMIfaceModem *self,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_model);

    mm_dbg ("loading model...");
    qmi_client_dms_get_model (QMI_CLIENT_DMS (client),
                              NULL,
                              5,
                              NULL,
                              (GAsyncReadyCallback)dms_get_model_ready,
                              result);
}

/*****************************************************************************/
/* Revision loading (Modem interface) */

static gchar *
modem_load_revision_finish (MMIfaceModem *self,
                            GAsyncResult *res,
                            GError **error)
{
    gchar *revision;

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    revision = g_strdup (g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res)));
    mm_dbg ("loaded revision: %s", revision);
    return revision;
}

static void
dms_get_revision_ready (QmiClientDms *client,
                        GAsyncResult *res,
                        GSimpleAsyncResult *simple)
{
    QmiMessageDmsGetRevisionOutput *output = NULL;
    GError *error = NULL;

    output = qmi_client_dms_get_revision_finish (client, res, &error);
    if (!output) {
        g_prefix_error (&error, "QMI operation failed: ");
        g_simple_async_result_take_error (simple, error);
    } else if (!qmi_message_dms_get_revision_output_get_result (output, &error)) {
        g_prefix_error (&error, "Couldn't get Revision: ");
        g_simple_async_result_take_error (simple, error);
    } else {
        const gchar *str;

        qmi_message_dms_get_revision_output_get_revision (output, &str, NULL);
        g_simple_async_result_set_op_res_gpointer (simple,
                                                   g_strdup (str),
                                                   (GDestroyNotify)g_free);
    }

    if (output)
        qmi_message_dms_get_revision_output_unref (output);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static void
modem_load_revision (MMIfaceModem *self,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
    GSimpleAsyncResult *result;
    QmiClient *client = NULL;

    if (!ensure_qmi_client (MM_BROADBAND_MODEM_QMI (self),
                            QMI_SERVICE_DMS, &client,
                            callback, user_data))
        return;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        modem_load_revision);

    mm_dbg ("loading revision...");
    qmi_client_dms_get_revision (QMI_CLIENT_DMS (client),
                                 NULL,
                                 5,
                                 NULL,
                                 (GAsyncReadyCallback)dms_get_revision_ready,
                                 result);
}

/*****************************************************************************/
/* First initialization step */

typedef struct {
    MMBroadbandModem *self;
    GSimpleAsyncResult *result;
    MMQmiPort *qmi;
    QmiService services[32];
    guint service_index;
} InitializationStartedContext;

static void
initialization_started_context_complete_and_free (InitializationStartedContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    if (ctx->qmi)
        g_object_unref (ctx->qmi);
    g_object_unref (ctx->result);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static gpointer
initialization_started_finish (MMBroadbandModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    /* Just parent's pointer passed here */
    return g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
}

static void
parent_initialization_started_ready (MMBroadbandModem *self,
                                     GAsyncResult *res,
                                     InitializationStartedContext *ctx)
{
    gpointer parent_ctx;
    GError *error = NULL;

    parent_ctx = MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_qmi_parent_class)->initialization_started_finish (
        self,
        res,
        &error);
    if (error) {
        g_prefix_error (&error, "Couldn't start parent initialization: ");
        g_simple_async_result_take_error (ctx->result, error);
    } else
        g_simple_async_result_set_op_res_gpointer (ctx->result, parent_ctx, NULL);

    initialization_started_context_complete_and_free (ctx);
}

static void
parent_initialization_started (InitializationStartedContext *ctx)
{
    MM_BROADBAND_MODEM_CLASS (mm_broadband_modem_qmi_parent_class)->initialization_started (
        ctx->self,
        (GAsyncReadyCallback)parent_initialization_started_ready,
        ctx);
}

static void allocate_next_client (InitializationStartedContext *ctx);

static void
qmi_port_allocate_client_ready (MMQmiPort *qmi,
                                GAsyncResult *res,
                                InitializationStartedContext *ctx)
{
    GError *error = NULL;

    if (!mm_qmi_port_allocate_client_finish (qmi, res, &error)) {
        mm_dbg ("Couldn't allocate client for service '%s': %s",
                qmi_service_get_string (ctx->services[ctx->service_index]),
                error->message);
        g_error_free (error);
    }

    ctx->service_index++;
    allocate_next_client (ctx);
}

static void
allocate_next_client (InitializationStartedContext *ctx)
{
    if (ctx->services[ctx->service_index] == QMI_SERVICE_UNKNOWN) {
        /* Done we are, launch parent's callback */
        parent_initialization_started (ctx);
        return;
    }

    /* Otherwise, allocate next client */
    mm_qmi_port_allocate_client (ctx->qmi,
                                 ctx->services[ctx->service_index],
                                 NULL,
                                 (GAsyncReadyCallback)qmi_port_allocate_client_ready,
                                 ctx);
}

static void
qmi_port_open_ready (MMQmiPort *qmi,
                     GAsyncResult *res,
                     InitializationStartedContext *ctx)
{
    GError *error = NULL;

    if (!mm_qmi_port_open_finish (qmi, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        initialization_started_context_complete_and_free (ctx);
        return;
    }

    allocate_next_client (ctx);
}

static void
initialization_started (MMBroadbandModem *self,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    InitializationStartedContext *ctx;

    ctx = g_new0 (InitializationStartedContext, 1);
    ctx->self = g_object_ref (self);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             initialization_started);
    ctx->qmi = mm_base_modem_get_port_qmi (MM_BASE_MODEM (self));
    g_assert (ctx->qmi);

    if (mm_qmi_port_is_open (ctx->qmi)) {
        /* Nothing to be done, just launch parent's callback */
        parent_initialization_started (ctx);
        return;
    }

    /* Setup services to open */
    ctx->services[0] = QMI_SERVICE_DMS;
    ctx->services[1] = QMI_SERVICE_WDS;
    ctx->services[2] = QMI_SERVICE_NAS;
    ctx->services[3] = QMI_SERVICE_UNKNOWN;

    /* Now open our QMI port */
    mm_qmi_port_open (ctx->qmi,
                      NULL,
                      (GAsyncReadyCallback)qmi_port_open_ready,
                      ctx);
}

/*****************************************************************************/

MMBroadbandModemQmi *
mm_broadband_modem_qmi_new (const gchar *device,
                            const gchar *driver,
                            const gchar *plugin,
                            guint16 vendor_id,
                            guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_QMI,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         NULL);
}

static void
mm_broadband_modem_qmi_init (MMBroadbandModemQmi *self)
{
}

static void
finalize (GObject *object)
{
    MMQmiPort *qmi;
    MMBroadbandModemQmi *self = MM_BROADBAND_MODEM_QMI (object);

    qmi = mm_base_modem_peek_port_qmi (MM_BASE_MODEM (self));
    /* If we did open the QMI port during initialization, close it now */
    if (qmi &&
        mm_qmi_port_is_open (qmi)) {
        mm_qmi_port_close (qmi);
    }

    G_OBJECT_CLASS (mm_broadband_modem_qmi_parent_class)->finalize (object);
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    /* Initialization steps */
    iface->load_current_capabilities = modem_load_current_capabilities;
    iface->load_current_capabilities_finish = modem_load_current_capabilities_finish;
    iface->load_manufacturer = modem_load_manufacturer;
    iface->load_manufacturer_finish = modem_load_manufacturer_finish;
    iface->load_model = modem_load_model;
    iface->load_model_finish = modem_load_model_finish;
    iface->load_revision = modem_load_revision;
    iface->load_revision_finish = modem_load_revision_finish;
}

static void
mm_broadband_modem_qmi_class_init (MMBroadbandModemQmiClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MMBroadbandModemClass *broadband_modem_class = MM_BROADBAND_MODEM_CLASS (klass);

    object_class->finalize = finalize;

    broadband_modem_class->initialization_started = initialization_started;
    broadband_modem_class->initialization_started_finish = initialization_started_finish;
}
