/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * libmm -- Access modem status & information from glib applications
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2012 Google, Inc.
 */

#include <gio/gio.h>

#include "mm-helpers.h"
#include "mm-modem-location.h"

/**
 * mm_modem_location_get_path:
 * @self: A #MMModemLocation.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 */
const gchar *
mm_modem_location_get_path (MMModemLocation *self)
{
    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_location_dup_path:
 * @self: A #MMModemLocation.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value should be freed with g_free().
 */
gchar *
mm_modem_location_dup_path (MMModemLocation *self)
{
    gchar *value;

    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);
    RETURN_NON_EMPTY_STRING (value);
}

MMModemLocationSource
mm_modem_location_get_capabilities (MMModemLocation *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self),
                          MM_MODEM_LOCATION_SOURCE_NONE);

    return (MMModemLocationSource) mm_gdbus_modem_location_get_capabilities (self);
}

gboolean
mm_modem_location_get_enabled (MMModemLocation *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_get_enabled (self);
}

gboolean
mm_modem_location_disable_finish (MMModemLocation *self,
                                  GAsyncResult *res,
                                  GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_call_enable_finish (self, res, error);
}

void
mm_modem_location_disable (MMModemLocation *self,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self));

    mm_gdbus_modem_location_call_enable (self,
                                         FALSE,
                                         FALSE,
                                         cancellable,
                                         callback,
                                         user_data);
}

gboolean
mm_modem_location_disable_sync (MMModemLocation *self,
                                GCancellable *cancellable,
                                GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_call_enable_sync (self,
                                                     FALSE,
                                                     FALSE,
                                                     cancellable,
                                                     error);
}

gboolean
mm_modem_location_enable_finish (MMModemLocation *self,
                                 GAsyncResult *res,
                                 GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_call_enable_finish (self, res, error);
}

void
mm_modem_location_enable (MMModemLocation *self,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self));

    mm_gdbus_modem_location_call_enable (self,
                                         TRUE,
                                         FALSE /* signal_location */,
                                         cancellable,
                                         callback,
                                         user_data);
}

gboolean
mm_modem_location_enable_sync (MMModemLocation *self,
                               GCancellable *cancellable,
                               GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self), FALSE);

    return mm_gdbus_modem_location_call_enable_sync (self,
                                                     TRUE,
                                                     FALSE /* signal_location */,
                                                     cancellable,
                                                     error);
}

static MMLocation3gpp *
build_3gpp_location (GVariant *dictionary,
                     GError **error)
{
    MMLocation3gpp *location = NULL;
    GError *inner_error = NULL;
    GVariant *value;
    guint source;
    GVariantIter iter;

    if (!dictionary)
        return NULL;

    g_variant_iter_init (&iter, dictionary);
    while (!location &&
           !inner_error &&
           g_variant_iter_next (&iter, "{uv}", &source, &value)) {
        /* If we have 3GPP LAC/CI location, build result */
        if (source == MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI)
            location = mm_location_3gpp_new_from_string_variant (value, &inner_error);

        g_variant_unref (value);
    }

    g_variant_unref (dictionary);

    if (inner_error)
        g_propagate_error (error, inner_error);

    return (MMLocation3gpp *)location;
}

MMLocation3gpp *
mm_modem_location_get_3gpp_finish (MMModemLocation *self,
                                   GAsyncResult *res,
                                   GError **error)
{

    GVariant *dictionary = NULL;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self), NULL);

    if (!mm_gdbus_modem_location_call_get_location_finish (self, &dictionary, res, error))
        return NULL;

    return build_3gpp_location (dictionary, error);
}

void
mm_modem_location_get_3gpp (MMModemLocation *self,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self));

    mm_gdbus_modem_location_call_get_location (self,
                                               cancellable,
                                               callback,
                                               user_data);
}

MMLocation3gpp *
mm_modem_location_get_3gpp_sync (MMModemLocation *self,
                                 GCancellable *cancellable,
                                 GError **error)
{
    GVariant *dictionary = NULL;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM_LOCATION (self), NULL);

    if (!mm_gdbus_modem_location_call_get_location_sync (self, &dictionary, cancellable, error))
        return NULL;

    return build_3gpp_location (dictionary, error);
}
