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
 */

#include <string.h>
#include <gmodule.h>
#include "mm-plugin-bandrich.h"
#include "mm-modem-bandrich.h"
#include "mm-generic-gsm.h"
#include "mm-generic-cdma.h"
#include "mm-modem-helpers.h"

G_DEFINE_TYPE (MMPluginBandrich, mm_plugin_bandrich, MM_TYPE_PLUGIN_BASE)

int mm_plugin_major_version = MM_PLUGIN_MAJOR_VERSION;
int mm_plugin_minor_version = MM_PLUGIN_MINOR_VERSION;

G_MODULE_EXPORT MMPlugin *
mm_plugin_create (void)
{
    return MM_PLUGIN (g_object_new (MM_TYPE_PLUGIN_BANDRICH,
                                    MM_PLUGIN_BASE_NAME, "Bandrich",
                                    NULL));
}

/*****************************************************************************/

static guint32
get_level_for_capabilities (guint32 capabilities)
{
    if (capabilities & MM_PLUGIN_BASE_PORT_CAP_GSM)
        return 10;
    return 0;
}

static void
probe_result (MMPluginBase *base,
              MMPluginBaseSupportsTask *task,
              guint32 capabilities,
              gpointer user_data)
{
    mm_plugin_base_supports_task_complete (task, get_level_for_capabilities (capabilities));
}

static MMPluginSupportsResult
supports_port (MMPluginBase *base,
               MMModem *existing,
               MMPluginBaseSupportsTask *task)
{
    GUdevDevice *port;
    const char *subsys, *name;
    guint16 vendor = 0, product = 0;

    port = mm_plugin_base_supports_task_get_port (task);

    subsys = g_udev_device_get_subsystem (port);
    g_assert (subsys);
    name = g_udev_device_get_name (port);
    g_assert (name);

    if (!mm_plugin_base_get_device_ids (base, subsys, name, &vendor, &product))
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    /* Vendor ID check */
    if (vendor != 0x1a8d)
        return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;

    /* The ethernet ports are obviously supported and don't need probing */
    if (!strcmp (subsys, "net")) {
        mm_plugin_base_supports_task_complete (task, 10);
        return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;
    }

    /* Otherwise kick off a probe */
    if (mm_plugin_base_probe_port (base, task, 0, NULL))
        return MM_PLUGIN_SUPPORTS_PORT_IN_PROGRESS;

    return MM_PLUGIN_SUPPORTS_PORT_UNSUPPORTED;
}

static MMModem *
grab_port (MMPluginBase *base,
           MMModem *existing,
           MMPluginBaseSupportsTask *task,
           GError **error)
{
    GUdevDevice *port = NULL;
    MMModem *modem = NULL;
    guint32 caps;
    const char *name, *subsys, *sysfs_path;
    MMPortType ptype;

    port = mm_plugin_base_supports_task_get_port (task);
    g_assert (port);

    subsys = g_udev_device_get_subsystem (port);
    name = g_udev_device_get_name (port);

    caps = mm_plugin_base_supports_task_get_probed_capabilities (task);
    if (!get_level_for_capabilities (caps)) {
        g_set_error (error, 0, 0, "Only GSM modems are currently supported by this plugin.");
        return NULL;
    }

    ptype = mm_plugin_base_probed_capabilities_to_port_type (caps);
    sysfs_path = mm_plugin_base_supports_task_get_physdev_path (task);
    if (!existing) {
        modem = mm_modem_bandrich_new (sysfs_path,
                                       mm_plugin_base_supports_task_get_driver (task),
                                       mm_plugin_get_name (MM_PLUGIN (base)));

        if (modem) {
            if (!mm_modem_grab_port (modem, subsys, name, ptype, MM_AT_PORT_FLAG_NONE, NULL, error)) {
                g_object_unref (modem);
                return NULL;
            }
        }
    } else {
        modem = existing;
        if (!mm_modem_grab_port (modem, subsys, name, ptype, MM_AT_PORT_FLAG_NONE, NULL, error))
            return NULL;
    }

    return modem;
}

/*****************************************************************************/

static void
mm_plugin_bandrich_init (MMPluginBandrich *self)
{
    g_signal_connect (self, "probe-result", G_CALLBACK (probe_result), NULL);
}

static void
mm_plugin_bandrich_class_init (MMPluginBandrichClass *klass)
{
    MMPluginBaseClass *pb_class = MM_PLUGIN_BASE_CLASS (klass);

    pb_class->supports_port = supports_port;
    pb_class->grab_port = grab_port;
}
