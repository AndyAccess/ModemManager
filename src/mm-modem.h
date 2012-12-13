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
 * Copyright (C) 2009 - 2010 Red Hat, Inc.
 */

#ifndef MM_MODEM_H
#define MM_MODEM_H

#include <glib-object.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <ModemManager.h>

#include "mm-port.h"
#include "mm-at-serial-port.h"
#include "mm-auth-provider.h"
#include "mm-charsets.h"

#define DBUS_PATH_TAG "dbus-path"

#define MM_TYPE_MODEM      (mm_modem_get_type ())
#define MM_MODEM(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM, MMModem))
#define MM_IS_MODEM(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM))
#define MM_MODEM_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE ((obj), MM_TYPE_MODEM, MMModem))

#define MM_MODEM_DATA_DEVICE   "device"
#define MM_MODEM_MASTER_DEVICE "master-device"
#define MM_MODEM_DRIVER        "driver"
#define MM_MODEM_TYPE          "type"
#define MM_MODEM_IP_METHOD     "ip-method"
#define MM_MODEM_IP_TIMEOUT    "ip-timeout"
#define MM_MODEM_SUPPORTED_IP_TYPES "supported-ip-types"
#define MM_MODEM_ENABLED       "enabled"
#define MM_MODEM_EQUIPMENT_IDENTIFIER "equipment-identifier"
#define MM_MODEM_DEVICE_IDENTIFIER "device-identifier"
#define MM_MODEM_UNLOCK_REQUIRED  "unlock-required"
#define MM_MODEM_UNLOCK_RETRIES   "unlock-retries"
#define MM_MODEM_PIN_RETRY_COUNTS   "pin-retry-counts"
#define MM_MODEM_STATE         "state"
#define MM_MODEM_VALID         "valid"      /* not exported */
#define MM_MODEM_PLUGIN        "plugin"     /* not exported */
#define MM_MODEM_HW_VID        "hw-vid"     /* not exported */
#define MM_MODEM_HW_PID        "hw-pid"     /* not exported */

#define MM_MODEM_UNLOCK_RETRIES_NOT_SUPPORTED 999

typedef enum {
    MM_MODEM_PROP_FIRST = 0x1000,

    MM_MODEM_PROP_DATA_DEVICE = MM_MODEM_PROP_FIRST,
    MM_MODEM_PROP_MASTER_DEVICE,
    MM_MODEM_PROP_DRIVER,
    MM_MODEM_PROP_TYPE,
    MM_MODEM_PROP_IP_METHOD,
    MM_MODEM_PROP_VALID,       /* Not exported */
    MM_MODEM_PROP_PLUGIN,      /* Not exported */
    MM_MODEM_PROP_STATE,
    MM_MODEM_PROP_ENABLED,
    MM_MODEM_PROP_EQUIPMENT_IDENTIFIER,
    MM_MODEM_PROP_UNLOCK_REQUIRED,
    MM_MODEM_PROP_UNLOCK_RETRIES,
    MM_MODEM_PROP_PIN_RETRY_COUNTS,
    MM_MODEM_PROP_DEVICE_IDENTIFIER,
    MM_MODEM_PROP_HW_VID,       /* Not exported */
    MM_MODEM_PROP_HW_PID,       /* Not exported */
    MM_MODEM_PROP_NETWORK_TIMEZONE,
    MM_MODEM_PROP_IP_TIMEOUT,
    MM_MODEM_PROP_SUPPORTED_IP_TYPES
} MMModemProp;

typedef struct _MMModem MMModem;

typedef void (*MMModemFn) (MMModem *modem,
                           GError *error,
                           gpointer user_data);

typedef void (*MMModemUIntFn) (MMModem *modem,
                               guint32 result,
                               GError *error,
                               gpointer user_data);

typedef void (*MMModemStringFn) (MMModem *modem,
                                 const char *result,
                                 GError *error,
                                 gpointer user_data);

typedef void (*MMModemIp4Fn) (MMModem *modem,
                              guint32 address,
                              guint32 netmask,
                              guint32 gateway,
                              GArray *dns,
                              GError *error,
                              gpointer user_data);

typedef void (*MMModemIp6Fn) (MMModem *modem,
                              const char *address,
                              guint32 prefix,
                              const char *gateway,
                              GPtrArray *dns,
                              GError *error,
                              gpointer user_data);

typedef void (*MMModemInfoFn) (MMModem *modem,
                               const char *manufacturer,
                               const char *model,
                               const char *version,
                               GError *error,
                               gpointer user_data);

typedef void (*MMModemArrayFn) (MMModem *modem,
                                GArray *items,
                                GError *error,
                                gpointer user_data);

struct _MMModem {
    GTypeInterface g_iface;

    /* Methods */
    gboolean (*owns_port) (MMModem *self,
                           const char *subsys,
                           const char *name);

    /* Subclasses use this function to claim a particular port */
    gboolean (*grab_port) (MMModem *self,
                           const char *subsys,
                           const char *name,
                           MMPortType ptype,
                           MMAtPortFlags at_pflags,
                           gpointer user_data,
                           GError **error);

    /* Subclasses use this function to determine which of their
     * grabbed ports should be used for data, command and status,
     * PPP, etc.  Called after all ports have been detected and
     * grabbed by the modem.
     */
    gboolean (*organize_ports) (MMModem *self,
                                GError **error);

    void (*release_port) (MMModem *self,
                          const char *subsys,
                          const char *name);

    void (*enable) (MMModem *self,
                    MMModemFn callback,
                    gpointer user_data);

    void (*disable) (MMModem *self,
                     MMModemFn callback,
                     gpointer user_data);

    void (*connect) (MMModem *self,
                     const char *number,
                     MMModemFn callback,
                     gpointer user_data);

    void (*get_ip4_config) (MMModem *self,
                            MMModemIp4Fn callback,
                            gpointer user_data);

    void (*get_ip6_config) (MMModem *self,
                            MMModemIp6Fn callback,
                            gpointer user_data);

    void (*disconnect) (MMModem *self,
                        MMModemStateReason reason,
                        MMModemFn callback,
                        gpointer user_data);

    void (*get_info) (MMModem *self,
                      MMModemInfoFn callback,
                      gpointer user_data);

    void (*get_supported_charsets) (MMModem *self,
                                    MMModemUIntFn callback,
                                    gpointer user_data);

    void (*set_charset) (MMModem *self,
                         MMModemCharset charset,
                         MMModemFn callback,
                         gpointer user_data);


    /* Normally implemented by the modem base class; plugins should
     * never need to implement this.
     */
    gboolean (*auth_request) (MMModem *self,
                              const char *authorization,
                              DBusGMethodInvocation *context,
                              MMAuthRequestCb callback,
                              gpointer callback_data,
                              GDestroyNotify notify,
                              GError **error);

    gboolean (*auth_finish)  (MMModem *self,
                              MMAuthRequest *req,
                              GError **error);

    void (*reset)            (MMModem *self,
                              MMModemFn callback,
                              gpointer user_data);

    void (*factory_reset) (MMModem *self,
                           const char *code,
                           MMModemFn callback,
                           gpointer user_data);

    /* Signals */
    void (*state_changed) (MMModem *self,
                           MMModemState old_state,
                           MMModemState new_state,
                           MMModemStateReason reason);
};

GType mm_modem_get_type (void);

gboolean mm_modem_owns_port (MMModem *self,
                             const char *subsys,
                             const char *name);

gboolean mm_modem_grab_port (MMModem *self,
                             const char *subsys,
                             const char *name,
                             MMPortType ptype,
                             MMAtPortFlags at_pflags,
                             gpointer user_data,
                             GError **error);

gboolean mm_modem_organize_ports (MMModem *self,
                                  GError **error);

void mm_modem_release_port (MMModem *self,
                            const char *subsys,
                            const char *name);

void mm_modem_enable (MMModem *self,
                      MMModemFn callback,
                      gpointer user_data);

void mm_modem_disable (MMModem *self,
                       MMModemFn callback,
                       gpointer user_data);

void mm_modem_connect (MMModem *self,
                       const char *number,
                       MMModemFn callback,
                       gpointer user_data);

void mm_modem_get_ip4_config (MMModem *self,
                              MMModemIp4Fn callback,
                              gpointer user_data);

void mm_modem_get_ip6_config (MMModem *self,
                              MMModemIp6Fn callback,
                              gpointer user_data);

void mm_modem_disconnect (MMModem *self,
                          MMModemStateReason reason,
                          MMModemFn callback,
                          gpointer user_data);

void mm_modem_get_info (MMModem *self,
                        MMModemInfoFn callback,
                        gpointer user_data);

void mm_modem_get_supported_charsets (MMModem *self,
                                      MMModemUIntFn callback,
                                      gpointer user_data);

void mm_modem_set_charset (MMModem *self,
                           MMModemCharset charset,
                           MMModemFn callback,
                           gpointer user_data);

void mm_modem_reset         (MMModem *self,
                             MMModemFn callback,
                             gpointer user_data);

void mm_modem_factory_reset (MMModem *self,
                             const char *code,
                             MMModemFn callback,
                             gpointer user_data);

gboolean mm_modem_get_valid (MMModem *self);

char *mm_modem_get_device (MMModem *self);

MMModemState mm_modem_get_state (MMModem *self);

void mm_modem_set_state (MMModem *self,
                         MMModemState new_state,
                         MMModemStateReason reason);

/* Request authorization to perform an action.  Used by D-Bus method
 * handlers to ensure that the incoming request is authorized to perform
 * the action it's requesting.
 */
gboolean mm_modem_auth_request (MMModem *self,
                                const char *authorization,
                                DBusGMethodInvocation *context,
                                MMAuthRequestCb callback,
                                gpointer callback_data,
                                GDestroyNotify notify,
                                GError **error);

gboolean mm_modem_auth_finish (MMModem *self,
                               MMAuthRequest *req,
                               GError **error);

#endif  /* MM_MODEM_H */

