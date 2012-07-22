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
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <ModemManager.h>
#include <mm-errors-types.h>

#include "mm-device.h"

#include "mm-log.h"

G_DEFINE_TYPE (MMDevice, mm_device, G_TYPE_OBJECT);

enum {
    PROP_0,
    PROP_UDEV_DEVICE,
    PROP_PLUGIN,
    PROP_MODEM,
    PROP_LAST
};

enum {
    SIGNAL_PORT_GRABBED,
    SIGNAL_PORT_RELEASED,
    SIGNAL_LAST
};

static GParamSpec *properties[PROP_LAST];
static guint signals[SIGNAL_LAST];

struct _MMDevicePrivate {
    /* Parent UDev device */
    GUdevDevice *udev_device;
    gchar *udev_device_path;

    /* Kernel driver managing this device */
    gchar *driver;

    /* Best plugin to manage this device */
    MMPlugin *plugin;

    /* List of port probes in the device */
    GList *port_probes;

    /* The Modem object for this device */
    MMBaseModem *modem;

    /* When exported, a reference to the object manager */
    GDBusObjectManagerServer *object_manager;
};

/*****************************************************************************/

static MMPortProbe *
device_find_probe_with_device (MMDevice    *self,
                               GUdevDevice *udev_port)
{
    GList *l;

    for (l = self->priv->port_probes; l; l = g_list_next (l)) {
        MMPortProbe *probe = MM_PORT_PROBE (l->data);

        if (g_str_equal (g_udev_device_get_sysfs_path (mm_port_probe_peek_port (probe)),
                         g_udev_device_get_sysfs_path (udev_port)))
            return probe;
    }

    return NULL;
}

gboolean
mm_device_owns_port (MMDevice    *self,
                     GUdevDevice *udev_port)
{
    return !!device_find_probe_with_device (self, udev_port);
}

static gchar *
get_driver_name (GUdevDevice *device)
{
    GUdevDevice *parent = NULL;
    const gchar *driver, *subsys;
    gchar *ret = NULL;

    driver = g_udev_device_get_driver (device);
    if (!driver) {
        parent = g_udev_device_get_parent (device);
        if (parent)
            driver = g_udev_device_get_driver (parent);

        /* Check for bluetooth; it's driver is a bunch of levels up so we
         * just check for the subsystem of the parent being bluetooth.
         */
        if (!driver && parent) {
            subsys = g_udev_device_get_subsystem (parent);
            if (subsys && !strcmp (subsys, "bluetooth"))
                driver = "bluetooth";
        }
    }

    if (driver)
        ret = g_strdup (driver);
    if (parent)
        g_object_unref (parent);

    return ret;
}

void
mm_device_grab_port (MMDevice    *self,
                     GUdevDevice *udev_port)
{
    MMPortProbe *probe;

    if (mm_device_owns_port (self, udev_port))
        return;

    /* Get the driver name out of the first port grabbed */
    if (!self->priv->port_probes)
        self->priv->driver = get_driver_name (udev_port);

    /* Create and store new port probe */
    probe = mm_port_probe_new (udev_port,
                               self->priv->udev_device_path,
                               self->priv->driver);
    self->priv->port_probes = g_list_prepend (self->priv->port_probes, probe);

    /* Notify about the grabbed port */
    g_signal_emit (self, signals[SIGNAL_PORT_GRABBED], 0, udev_port);
}

void
mm_device_release_port (MMDevice    *self,
                        GUdevDevice *udev_port)
{
    MMPortProbe *probe;

    probe = device_find_probe_with_device (self, udev_port);
    if (probe) {
        /* Found, remove from list and destroy probe */
        self->priv->port_probes = g_list_remove (self->priv->port_probes, probe);
        g_signal_emit (self, signals[SIGNAL_PORT_RELEASED], 0, mm_port_probe_peek_port (probe));
        g_object_unref (probe);
    }
}

/*****************************************************************************/

static void
unexport_modem (MMDevice *self)
{
    gchar *path;

    g_assert (MM_IS_BASE_MODEM (self->priv->modem));
    g_assert (G_IS_DBUS_OBJECT_MANAGER (self->priv->object_manager));

    path = g_strdup (g_dbus_object_get_object_path (G_DBUS_OBJECT (self->priv->modem)));
    if (path != NULL) {
        g_dbus_object_manager_server_unexport (self->priv->object_manager, path);
        g_object_set (self->priv->modem,
                      MM_BASE_MODEM_CONNECTION, NULL,
                      NULL);

        mm_dbg ("Unexported modem '%s' from path '%s'",
                g_udev_device_get_sysfs_path (self->priv->udev_device),
                path);
        g_free (path);
    }
}

/*****************************************************************************/

static void
export_modem (MMDevice *self)
{
    GDBusConnection *connection = NULL;
    static guint32 id = 0;
    gchar *path;

    g_assert (MM_IS_BASE_MODEM (self->priv->modem));
    g_assert (G_IS_DBUS_OBJECT_MANAGER (self->priv->object_manager));

    /* If modem not yet valid (not fully initialized), don't export it */
    if (!mm_base_modem_get_valid (self->priv->modem)) {
        mm_dbg ("Modem '%s' not yet fully initialized",
                g_udev_device_get_sysfs_path (self->priv->udev_device));
        return;
    }

    /* Don't export already exported modems */
    g_object_get (self->priv->modem,
                  "g-object-path", &path,
                  NULL);
    if (path) {
        g_free (path);
        mm_dbg ("Modem '%s' already exported",
                g_udev_device_get_sysfs_path (self->priv->udev_device));
        return;
    }

    /* No outstanding port tasks, so if the modem is valid we can export it */

    path = g_strdup_printf (MM_DBUS_MODEM_PREFIX "/%d", id++);
    g_object_get (self->priv->object_manager,
                  "connection", &connection,
                  NULL);
    g_object_set (self->priv->modem,
                  "g-object-path", path,
                  MM_BASE_MODEM_CONNECTION, connection,
                  NULL);
    g_object_unref (connection);

    g_dbus_object_manager_server_export (self->priv->object_manager,
                                         G_DBUS_OBJECT_SKELETON (self->priv->modem));

    mm_dbg ("Exported modem '%s' at path '%s'",
            g_udev_device_get_sysfs_path (self->priv->udev_device),
            path);

    /* Once connected, dump additional debug info about the modem */
    mm_dbg ("(%s): '%s' modem, VID 0x%04X PID 0x%04X (%s)",
            path,
            mm_base_modem_get_plugin (self->priv->modem),
            (mm_base_modem_get_vendor_id (self->priv->modem) & 0xFFFF),
            (mm_base_modem_get_product_id (self->priv->modem) & 0xFFFF),
            g_udev_device_get_subsystem (self->priv->udev_device));

    g_free (path);
}

/*****************************************************************************/

void
mm_device_remove_modem (MMDevice  *self)
{
    if (!self->priv->modem)
        return;

    unexport_modem (self);

    /* Run dispose before unref-ing, in order to cleanup the SIM object,
     * if any (which also holds a reference to the modem object) */
    g_object_run_dispose (G_OBJECT (self->priv->modem));
    g_clear_object (&(self->priv->modem));
    g_clear_object (&(self->priv->object_manager));
}

/*****************************************************************************/

static void
modem_valid (MMBaseModem *modem,
             GParamSpec  *pspec,
             MMDevice    *self)
{
    if (!mm_base_modem_get_valid (modem)) {
        /* Modem no longer valid */
        mm_device_remove_modem (self);
    } else {
        /* Modem now valid, export it */
        export_modem (self);
    }
}

gboolean
mm_device_create_modem (MMDevice                  *self,
                        GDBusObjectManagerServer  *object_manager,
                        GError                   **error)
{
    g_assert (self->priv->modem == NULL);
    g_assert (self->priv->object_manager == NULL);
    g_assert (self->priv->port_probes != NULL);

    mm_info ("Creating modem with plugin '%s' and '%u' ports",
             mm_plugin_get_name (self->priv->plugin),
             g_list_length (self->priv->port_probes));

    self->priv->modem = mm_plugin_create_modem (self->priv->plugin,
                                                G_OBJECT (self),
                                                error);
    if (self->priv->modem) {
        /* Keep the object manager */
        self->priv->object_manager = g_object_ref (object_manager);

        /* We want to get notified when the modem becomes valid/invalid */
        g_signal_connect (self->priv->modem,
                          "notify::" MM_BASE_MODEM_VALID,
                          G_CALLBACK (modem_valid),
                          self);
    }

    return !!self->priv->modem;
}

/*****************************************************************************/

const gchar *
mm_device_get_path (MMDevice *self)
{
    return self->priv->udev_device_path;
}

const gchar *
mm_device_get_driver (MMDevice *self)
{
    return self->priv->driver;
}

GUdevDevice *
mm_device_peek_udev_device (MMDevice *self)
{
    return self->priv->udev_device;
}

GUdevDevice *
mm_device_get_udev_device (MMDevice *self)
{
    return G_UDEV_DEVICE (g_object_ref (self->priv->udev_device));
}

void
mm_device_set_plugin (MMDevice *self,
                      MMPlugin *plugin)
{
    g_object_set (self,
                  MM_DEVICE_PLUGIN, plugin,
                  NULL);
}

MMPlugin *
mm_device_peek_plugin (MMDevice *self)
{
    return self->priv->plugin;
}

MMPlugin *
mm_device_get_plugin (MMDevice *self)
{
    return (self->priv->plugin ?
            MM_PLUGIN (g_object_ref (self->priv->plugin)) :
            NULL);
}

MMBaseModem *
mm_device_peek_modem (MMDevice *self)
{
    return (self->priv->modem ?
            MM_BASE_MODEM (self->priv->modem) :
            NULL);
}

MMBaseModem *
mm_device_get_modem (MMDevice *self)
{
    return (self->priv->modem ?
            MM_BASE_MODEM (g_object_ref (self->priv->modem)) :
            NULL);
}

MMPortProbe *
mm_device_peek_port_probe (MMDevice *self,
                           GUdevDevice *udev_port)
{
    return device_find_probe_with_device (self, udev_port);
}

MMPortProbe *
mm_device_get_port_probe (MMDevice *self,
                          GUdevDevice *udev_port)
{
    MMPortProbe *probe;

    probe = device_find_probe_with_device (self, udev_port);
    return (probe ? g_object_ref (probe) : NULL);
}

GList *
mm_device_peek_port_probe_list (MMDevice *self)
{
    return self->priv->port_probes;
}

GList *
mm_device_get_port_probe_list (MMDevice *self)
{
    GList *copy;

    copy = g_list_copy (self->priv->port_probes);
    g_list_foreach (copy, (GFunc)g_object_ref, NULL);
    return copy;
}

/*****************************************************************************/

MMDevice *
mm_device_new (GUdevDevice *udev_device)
{
    return MM_DEVICE (g_object_new (MM_TYPE_DEVICE,
                                    MM_DEVICE_UDEV_DEVICE, udev_device,
                                    NULL));
}

static void
mm_device_init (MMDevice *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_DEVICE,
                                              MMDevicePrivate);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMDevice *self = MM_DEVICE (object);

    switch (prop_id) {
    case PROP_UDEV_DEVICE:
        /* construct only */
        self->priv->udev_device = g_value_dup_object (value);
        self->priv->udev_device_path = g_strdup (g_udev_device_get_sysfs_path (self->priv->udev_device));
        break;
    case PROP_PLUGIN:
        g_clear_object (&(self->priv->plugin));
        self->priv->plugin = g_value_dup_object (value);
        break;
    case PROP_MODEM:
        g_clear_object (&(self->priv->modem));
        self->priv->modem = g_value_dup_object (value);
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
    MMDevice *self = MM_DEVICE (object);

    switch (prop_id) {
    case PROP_UDEV_DEVICE:
        g_value_set_object (value, self->priv->udev_device);
        break;
    case PROP_PLUGIN:
        g_value_set_object (value, self->priv->plugin);
        break;
    case PROP_MODEM:
        g_value_set_object (value, self->priv->modem);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
dispose (GObject *object)
{
    MMDevice *self = MM_DEVICE (object);

    g_clear_object (&(self->priv->udev_device));
    g_clear_object (&(self->priv->plugin));
    g_list_free_full (self->priv->port_probes, (GDestroyNotify)g_object_unref);
    g_clear_object (&(self->priv->modem));

    G_OBJECT_CLASS (mm_device_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
    MMDevice *self = MM_DEVICE (object);

    g_free (self->priv->udev_device_path);
    g_free (self->priv->driver);

    G_OBJECT_CLASS (mm_device_parent_class)->finalize (object);
}

static void
mm_device_class_init (MMDeviceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMDevicePrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->finalize = finalize;
    object_class->dispose = dispose;

    properties[PROP_UDEV_DEVICE] =
        g_param_spec_object (MM_DEVICE_UDEV_DEVICE,
                             "UDev Device",
                             "UDev device object",
                             G_UDEV_TYPE_DEVICE,
                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property (object_class, PROP_UDEV_DEVICE, properties[PROP_UDEV_DEVICE]);

    properties[PROP_PLUGIN] =
        g_param_spec_object (MM_DEVICE_PLUGIN,
                             "Plugin",
                             "Best plugin to manage this device",
                             MM_TYPE_PLUGIN,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_PLUGIN, properties[PROP_PLUGIN]);

    properties[PROP_MODEM] =
        g_param_spec_object (MM_DEVICE_MODEM,
                             "Modem",
                             "The modem object",
                             MM_TYPE_BASE_MODEM,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MODEM, properties[PROP_MODEM]);

    signals[SIGNAL_PORT_GRABBED] =
        g_signal_new (MM_DEVICE_PORT_GRABBED,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMDeviceClass, port_grabbed),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 1, G_UDEV_TYPE_DEVICE);

    signals[SIGNAL_PORT_RELEASED] =
        g_signal_new (MM_DEVICE_PORT_RELEASED,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMDeviceClass, port_released),
                      NULL, NULL,
                      g_cclosure_marshal_generic,
                      G_TYPE_NONE, 1, G_UDEV_TYPE_DEVICE);
}
