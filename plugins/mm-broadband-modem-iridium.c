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
 * Copyright (C) 2011 - 2012 Ammonit Measurement GmbH
 * Author: Aleksander Morgado <aleksander@lanedo.com>
 */

#include <config.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "ModemManager.h"
#include "mm-log.h"
#include "mm-errors-types.h"
#include "mm-base-modem-at.h"
#include "mm-iface-modem.h"
#include "mm-iface-modem-messaging.h"
#include "mm-broadband-modem-iridium.h"
#include "mm-sim-iridium.h"

static void iface_modem_init (MMIfaceModem *iface);

G_DEFINE_TYPE_EXTENDED (MMBroadbandModemIridium, mm_broadband_modem_iridium, MM_TYPE_BROADBAND_MODEM, 0,
                        G_IMPLEMENT_INTERFACE (MM_TYPE_IFACE_MODEM, iface_modem_init));

/*****************************************************************************/
/* Flow control (Modem interface) */

static gboolean
setup_flow_control_finish (MMIfaceModem *self,
                           GAsyncResult *res,
                           GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
setup_flow_control_ready (MMBroadbandModemIridium *self,
                          GAsyncResult *res,
                          GSimpleAsyncResult *operation_result)
{
    GError *error = NULL;

    if (!mm_base_modem_at_command_finish (MM_BASE_MODEM (self), res, &error))
        /* Let the error be critical. We DO need RTS/CTS in order to have
         * proper modem disabling. */
        g_simple_async_result_take_error (operation_result, error);
    else
        g_simple_async_result_set_op_res_gboolean (operation_result, TRUE);

    g_simple_async_result_complete (operation_result);
    g_object_unref (operation_result);
}

static void
setup_flow_control (MMIfaceModem *self,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        setup_flow_control);

    /* Enable RTS/CTS flow control.
     * Other available values:
     *   AT&K0: Disable flow control
     *   AT&K3: RTS/CTS
     *   AT&K4: XOFF/XON
     *   AT&K6: Both RTS/CTS and XOFF/XON
     */
    mm_base_modem_at_command (MM_BASE_MODEM (self),
                              "&K3",
                              3,
                              FALSE,
                              NULL, /* cancellable */
                              (GAsyncReadyCallback)setup_flow_control_ready,
                              result);
}

/*****************************************************************************/
/* Create SIM (Modem inteface) */

static MMSim *
create_sim_finish (MMIfaceModem *self,
                   GAsyncResult *res,
                   GError **error)
{
    return mm_sim_new_finish (res, error);
}

static void
create_sim (MMIfaceModem *self,
            GAsyncReadyCallback callback,
            gpointer user_data)
{
    /* New Iridium SIM */
    mm_sim_iridium_new (MM_BASE_MODEM (self),
                        NULL, /* cancellable */
                        callback,
                        user_data);
}

/*****************************************************************************/

MMBroadbandModemIridium *
mm_broadband_modem_iridium_new (const gchar *device,
                                const gchar *driver,
                                const gchar *plugin,
                                guint16 vendor_id,
                                guint16 product_id)
{
    return g_object_new (MM_TYPE_BROADBAND_MODEM_IRIDIUM,
                         MM_BASE_MODEM_DEVICE, device,
                         MM_BASE_MODEM_DRIVER, driver,
                         MM_BASE_MODEM_PLUGIN, plugin,
                         MM_BASE_MODEM_VENDOR_ID, vendor_id,
                         MM_BASE_MODEM_PRODUCT_ID, product_id,
                         MM_IFACE_MODEM_MESSAGING_SMS_MEM1_STORAGE, MM_SMS_STORAGE_SM,
                         MM_IFACE_MODEM_MESSAGING_SMS_MEM2_STORAGE, MM_SMS_STORAGE_SM,
                         MM_IFACE_MODEM_MESSAGING_SMS_MEM3_STORAGE, MM_SMS_STORAGE_SM,
                         NULL);
}

static void
mm_broadband_modem_iridium_init (MMBroadbandModemIridium *self)
{
}

static void
iface_modem_init (MMIfaceModem *iface)
{
    /* Create Iridium-specific SIM */
    iface->create_sim = create_sim;
    iface->create_sim_finish = create_sim_finish;

    /* RTS/CTS flow control */
    iface->setup_flow_control = setup_flow_control;
    iface->setup_flow_control_finish = setup_flow_control_finish;

    /* No need to power-up/power-down the modem */
    iface->modem_power_up = NULL;
    iface->modem_power_up_finish = NULL;
    iface->modem_power_down = NULL;
    iface->modem_power_down_finish = NULL;
}

static void
mm_broadband_modem_iridium_class_init (MMBroadbandModemIridiumClass *klass)
{
}
