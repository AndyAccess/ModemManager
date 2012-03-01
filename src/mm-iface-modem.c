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

#include <mm-gdbus-modem.h>
#include <mm-errors-types.h>

#include "mm-iface-modem.h"
#include "mm-base-modem.h"
#include "mm-log.h"

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (InitializationContext *ctx);

typedef enum {
    INTERFACE_STATUS_SHUTDOWN,
    INTERFACE_STATUS_INITIALIZING,
    INTERFACE_STATUS_INITIALIZED
} InterfaceStatus;

static gboolean
handle_create_bearer (MmGdbusModem *object,
                      GDBusMethodInvocation *invocation,
                      GVariant *arg_properties,
                      MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

static gboolean
handle_delete_bearer (MmGdbusModem *object,
                      GDBusMethodInvocation *invocation,
                      const gchar *arg_bearer,
                      MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

static gboolean
handle_list_bearers (MmGdbusModem *object,
                     GDBusMethodInvocation *invocation,
                     MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

static gboolean
handle_enable (MmGdbusModem *object,
               GDBusMethodInvocation *invocation,
               gboolean arg_enable,
               MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

static gboolean
handle_reset (MmGdbusModem *object,
              GDBusMethodInvocation *invocation,
              MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

static gboolean
handle_factory_reset (MmGdbusModem *object,
                      GDBusMethodInvocation *invocation,
                      const gchar *arg_code,
                      MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

static gboolean
handle_set_allowed_bands (MmGdbusModem *object,
                          GDBusMethodInvocation *invocation,
                          guint64 arg_bands,
                          MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

static gboolean
handle_set_allowed_modes (MmGdbusModem *object,
                          GDBusMethodInvocation *invocation,
                          guint arg_modes,
                          guint arg_preferred,
                          MMIfaceModem *self)
{
    return FALSE; /* Currently unhandled */
}

/*****************************************************************************/

typedef enum {
    INITIALIZATION_STEP_FIRST,
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
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    InitializationContext *ctx;

    ctx = g_new0 (InitializationContext, 1);
    ctx->self = g_object_ref (self);
    ctx->port = g_object_ref (mm_base_modem_get_port_primary (MM_BASE_MODEM (self)));
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
initialization_context_free (InitializationContext *ctx)
{
    g_object_unref (ctx->self);
    g_object_unref (ctx->port);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static gboolean
interface_initialization_finish (MMIfaceModem *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
interface_initialization_step (InitializationContext *ctx)
{
    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST: {
        break;
    }
    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        g_simple_async_result_complete_in_idle (ctx->result);
        mm_serial_port_close (MM_SERIAL_PORT (ctx->port));
        initialization_context_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
interface_initialization (MMIfaceModem *self,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    InitializationContext *ctx;
    GError *error = NULL;

    ctx = initialization_context_new (self, callback, user_data);

    if (!mm_serial_port_open (MM_SERIAL_PORT (ctx->port), &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        g_simple_async_result_complete_in_idle (ctx->result);
        initialization_context_free (ctx);
        return;
    }

    /* Try to disable echo */
    mm_at_serial_port_queue_command (ctx->port, "E0", 3, NULL, NULL);
    /* Try to get extended errors */
    mm_at_serial_port_queue_command (ctx->port, "+CMEE=1", 2, NULL, NULL);

    interface_initialization_step (ctx);
}

/*****************************************************************************/


static InterfaceStatus
get_status (MMIfaceModem *self)
{
    GObject *skeleton = NULL;

    /* Are we already disabled? */
    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton)
        return INTERFACE_STATUS_SHUTDOWN;
    g_object_unref (skeleton);

    /* Are we being initialized? (interface not yet exported) */
    skeleton = G_OBJECT (mm_gdbus_object_get_modem (MM_GDBUS_OBJECT (self)));
    if (skeleton) {
        g_object_unref (skeleton);
        return INTERFACE_STATUS_INITIALIZED;
    }

    return INTERFACE_STATUS_INITIALIZING;
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

static void
interface_initialization_ready (MMIfaceModem *self,
                                GAsyncResult *init_result,
                                GSimpleAsyncResult *op_result)
{
    GObject *skeleton = NULL;
    GError *inner_error = NULL;

    /* If initialization failed, remove the skeleton and return the error */
    if (!interface_initialization_finish (self,
                                          init_result,
                                          &inner_error)) {
        g_object_set (self,
                      MM_IFACE_MODEM_DBUS_SKELETON, NULL,
                      NULL);
        g_simple_async_result_take_error (op_result, inner_error);
        g_simple_async_result_complete (op_result);
        g_object_unref (op_result);
        return;
    }

    /* Finish current initialization by setting up the DBus skeleton */
    g_object_get (self,
                  MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                  NULL);
    g_assert (skeleton != NULL);

    /* Handle method invocations */
    g_signal_connect (skeleton,
                      "handle-create-bearer",
                      G_CALLBACK (handle_create_bearer),
                      self);
    g_signal_connect (skeleton,
                      "handle-delete-bearer",
                      G_CALLBACK (handle_delete_bearer),
                      self);
    g_signal_connect (skeleton,
                      "handle-list-bearers",
                      G_CALLBACK (handle_list_bearers),
                      self);
    g_signal_connect (skeleton,
                      "handle-enable",
                      G_CALLBACK (handle_enable),
                      self);
    g_signal_connect (skeleton,
                      "handle-reset",
                      G_CALLBACK (handle_reset),
                      self);
    g_signal_connect (skeleton,
                      "handle-factory-reset",
                      G_CALLBACK (handle_factory_reset),
                      self);
    g_signal_connect (skeleton,
                      "handle-set-allowed-bands",
                      G_CALLBACK (handle_set_allowed_bands),
                      self);
    g_signal_connect (skeleton,
                      "handle-set-allowed-modes",
                      G_CALLBACK (handle_set_allowed_modes),
                      self);

    /* Finally, export the new interface */
    mm_gdbus_object_skeleton_set_modem (MM_GDBUS_OBJECT_SKELETON (self),
                                        MM_GDBUS_MODEM (skeleton));
    g_simple_async_result_set_op_res_gboolean (op_result, TRUE);
    g_simple_async_result_complete (op_result);
    g_object_unref (op_result);
}

void
mm_iface_modem_initialize (MMIfaceModem *self,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    GSimpleAsyncResult *result;

    g_return_if_fail (MM_IS_IFACE_MODEM (self));

    /* Setup asynchronous result */
    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_iface_modem_initialize);

    switch (get_status (self)) {
    case INTERFACE_STATUS_INITIALIZED:
    case INTERFACE_STATUS_SHUTDOWN: {
        MmGdbusModem *skeleton = NULL;

        /* Did we already create it? */
        g_object_get (self,
                      MM_IFACE_MODEM_DBUS_SKELETON, &skeleton,
                      NULL);
        if (!skeleton) {
            skeleton = mm_gdbus_modem_skeleton_new ();

            /* Set all initial property defaults */
            mm_gdbus_modem_set_sim (skeleton, NULL);
            mm_gdbus_modem_set_modem_capabilities (skeleton, MM_MODEM_CAPABILITY_NONE);
            mm_gdbus_modem_set_current_capabilities (skeleton, MM_MODEM_CAPABILITY_NONE);
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
            mm_gdbus_modem_set_access_technology (skeleton, MM_MODEM_ACCESS_TECH_UNKNOWN);
            mm_gdbus_modem_set_signal_quality (skeleton, g_variant_new ("(ub)", 0, FALSE));
            mm_gdbus_modem_set_supported_modes (skeleton, MM_MODEM_MODE_NONE);
            mm_gdbus_modem_set_allowed_modes (skeleton, MM_MODEM_MODE_ANY);
            mm_gdbus_modem_set_preferred_mode (skeleton, MM_MODEM_MODE_NONE);
            mm_gdbus_modem_set_supported_bands (skeleton, MM_MODEM_BAND_UNKNOWN);
            mm_gdbus_modem_set_allowed_bands (skeleton, MM_MODEM_BAND_ANY);
            mm_gdbus_modem_set_state (skeleton, MM_MODEM_STATE_UNKNOWN);

            /* Keep a reference to it */
            g_object_set (self,
                          MM_IFACE_MODEM_DBUS_SKELETON, skeleton,
                          NULL);
        }

        /* Perform async initialization here */
        interface_initialization (self,
                                  (GAsyncReadyCallback)interface_initialization_ready,
                                  result);
        g_object_unref (skeleton);
        return;
    }

    case INTERFACE_STATUS_INITIALIZING:
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_IN_PROGRESS,
                                         "Interface is already being enabled");
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    g_return_if_reached ();
}

gboolean
mm_iface_modem_shutdown (MMIfaceModem *self,
                         GError **error)
{
    g_return_val_if_fail (MM_IS_IFACE_MODEM (self), FALSE);

    switch (get_status (self)) {
    case INTERFACE_STATUS_SHUTDOWN:
        return TRUE;
    case INTERFACE_STATUS_INITIALIZING:
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_IN_PROGRESS,
                     "Iinterface being currently initialized");
        return FALSE;
    case INTERFACE_STATUS_INITIALIZED:
        /* Remove SIM object */
        g_object_set (self,
                      MM_IFACE_MODEM_SIM, NULL,
                      NULL);
        /* Unexport DBus interface and remove the skeleton */
        mm_gdbus_object_skeleton_set_modem (MM_GDBUS_OBJECT_SKELETON (self), NULL);
        g_object_set (self,
                      MM_IFACE_MODEM_DBUS_SKELETON, NULL,
                      NULL);
        return TRUE;
    }

    g_return_val_if_reached (FALSE);
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
