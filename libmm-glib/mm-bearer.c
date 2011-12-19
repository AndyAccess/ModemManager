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
 * Copyright (C) 2011 Aleksander Morgado <aleksander@gnu.org>
 */

#include "mm-helpers.h"
#include "mm-bearer.h"

/**
 * mm_bearer_get_path:
 * @self: A #MMBearer.
 *
 * Gets the DBus path of the #MMBearer object.
 *
 * Returns: (transfer none): The DBus path of the #MMBearer object.
 */
const gchar *
mm_bearer_get_path (MMBearer *self)
{
    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_bearer_dup_path:
 * @self: A #MMBearer.
 *
 * Gets a copy of the DBus path of the #MMBearer object.
 *
 * Returns: (transfer full): The DBus path of the #MMBearer object. The returned value should be freed with g_free().
 */
gchar *
mm_bearer_dup_path (MMBearer *self)
{
    gchar *value;

    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);

    RETURN_NON_EMPTY_STRING (value);
}

/**
 * mm_bearer_get_interface:
 * @self: A #MMBearer.
 *
 * Gets the operating system name for the network data interface that provides
 * packet data using this #MMBearer. This will only be available once the #MMBearer
 * is in connected state.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_bearer_dup_interface() if on another thread.</warning>
 *
 * Returns: (transfer none): The name of the interface, or %NULL if it couldn't be retrieved.
 */
const gchar *
mm_bearer_get_interface (MMBearer *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_bearer_get_interface (self));
}

/**
 * mm_bearer_dup_interface:
 * @self: A #MMBearer.
 *
 * Gets a copy of the operating system name for the network data interface that provides
 * packet data using this #MMBearer. This will only be available once the #MMBearer
 * is in connected state.
 *
 * Returns: (transfer full): The name of the interface, or %NULL if it couldn't be retrieved. The returned value should be freed with g_free().
 */
gchar *
mm_bearer_dup_interface (MMBearer *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_bearer_dup_interface (self));
}

/**
 * mm_bearer_get_connected:
 * @self: A #MMBearer.
 *
 * Checks whether or not the #MMBearer is connected and thus whether packet data
 * communication is possible.
 *
 * Returns: %TRUE if the #MMBearer is connected, #FALSE otherwise.
 */
gboolean
mm_bearer_get_connected (MMBearer *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_get_connected (self);
}

/**
 * mm_bearer_get_suspended:
 * @self: A #MMBearer.
 *
 * Checks whether or not the #MMBearer is suspended (but not deactivated) while the
 * device is handling other communications, like a voice call.
 *
 * Returns: %TRUE if packet data service is suspended in the #MMBearer, #FALSE otherwise.
 */
gboolean
mm_bearer_get_suspended (MMBearer *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_get_suspended (self);
}

struct _MMBearerIpConfig {
    MMBearerIpMethod method;
    gchar *address;
    guint32 prefix;
    gchar *dns[4];
    gchar *gateway;
};

void
mm_bearer_ip_config_free (MMBearerIpConfig *config)
{
    if (!config)
        return;

    g_free (config->address);
    g_free (config->gateway);
    g_free (config->dns[0]);
    g_free (config->dns[1]);
    g_free (config->dns[2]);
    g_slice_free (MMBearerIpConfig, config);
}

MMBearerIpMethod
mm_bearer_ip_config_get_method (const MMBearerIpConfig *config)
{
    g_return_val_if_fail (config != NULL, MM_BEARER_IP_METHOD_UNKNOWN);

    return config->method;
}

const gchar *
mm_bearer_ip_config_get_address (const MMBearerIpConfig *config)
{
    g_return_val_if_fail (config != NULL, NULL);

    return config->address;
}

gchar *
mm_bearer_ip_config_dup_address (const MMBearerIpConfig *config)
{
    g_return_val_if_fail (config != NULL, NULL);

    return g_strdup (config->address);
}

guint
mm_bearer_ip_config_get_prefix (const MMBearerIpConfig *config)
{
    g_return_val_if_fail (config != NULL, 0);

    return config->prefix;
}

const gchar **
mm_bearer_ip_config_get_dns (const MMBearerIpConfig *config)
{
    g_return_val_if_fail (config != NULL, NULL);

    return (const gchar **)config->dns;
}

gchar **
mm_bearer_ip_config_dup_dns (const MMBearerIpConfig *config)
{
    g_return_val_if_fail (config != NULL, NULL);

    return g_strdupv ((gchar **)config->dns);
}

const gchar *
mm_bearer_ip_config_get_gateway (const MMBearerIpConfig *config)
{
    g_return_val_if_fail (config != NULL, NULL);

    return config->gateway;
}

gchar *
mm_bearer_ip_config_dup_gateway (const MMBearerIpConfig *config)
{
    g_return_val_if_fail (config != NULL, NULL);

    return g_strdup (config->gateway);
}

static MMBearerIpConfig *
create_ip_config_struct (GVariant *variant)
{
    GVariantIter iter;
    const gchar *key;
    GVariant *value;
    MMBearerIpConfig *c;

    c = g_slice_new0 (MMBearerIpConfig);

    g_variant_iter_init (&iter, variant);
    while (g_variant_iter_loop (&iter, "{sv}", &key, &value)) {
        if (g_str_equal (key, "method")) {
            if (c->method)
                g_warning ("Duplicate 'method' key found in Bearer IP configuration");
            else
                c->method = (MMBearerIpMethod) g_variant_get_uint32 (value);
        } else if (g_str_equal (key, "address")) {
            if (c->address)
                g_warning ("Duplicate 'address' key found in Bearer IP configuration");
            else
                c->address = g_variant_dup_string (value, NULL);
        } else if (g_str_equal (key, "prefix")) {
            if (c->prefix)
                g_warning ("Duplicate 'prefix' key found in Bearer IP configuration");
            else
                c->prefix = g_variant_get_uint32 (value);
        } else if (g_str_equal (key, "dns1")) {
            if (c->dns[0])
                g_warning ("Duplicate 'dns1' key found in Bearer IP configuration");
            else
                c->dns[0] = g_variant_dup_string (value, NULL);
        } else if (g_str_equal (key, "dns2")) {
            if (c->dns[1])
                g_warning ("Duplicate 'dns2' key found in Bearer IP configuration");
            else
                c->dns[1] = g_variant_dup_string (value, NULL);
        } else if (g_str_equal (key, "dns3")) {
            if (c->dns[2])
                g_warning ("Duplicate 'dns3' key found in Bearer IP configuration");
            else
                c->dns[2] = g_variant_dup_string (value, NULL);
        } else if (g_str_equal (key, "gateway")) {
            if (c->gateway)
                g_warning ("Duplicate 'gateway' key found in Bearer IP configuration");
            else
                c->gateway = g_variant_dup_string (value, NULL);
        } else
            g_warning ("Invalid property '%s' found in Bearer IP configuration", key);
    }

    /* If no method is set, don't build the config struct */
    if (!c->method) {
        mm_bearer_ip_config_free (c);
        return NULL;
    }

    return c;
}

#define MM_BEARER_IPV4_CONFIG_DATA "bearer-ipv4"

static void
ipv4_config_changed (MMBearer *self)
{
    g_object_set_data (G_OBJECT (self), MM_BEARER_IPV4_CONFIG_DATA, NULL);
}

const MMBearerIpConfig *
mm_bearer_get_ipv4_config (MMBearer *self)
{
    MMBearerIpConfig *config;

    g_return_val_if_fail (MM_IS_BEARER (self), NULL);

    config = g_object_get_data (G_OBJECT (self),
                                MM_BEARER_IPV4_CONFIG_DATA);
    if (!config) {
        config = create_ip_config_struct (
            mm_gdbus_bearer_get_ip4_config (MM_GDBUS_BEARER (self)));

        /* Note: if no specific method set, config struct will not be created */
        if (config) {
            g_object_set_data_full (G_OBJECT (self),
                                    MM_BEARER_IPV4_CONFIG_DATA,
                                    config,
                                    (GDestroyNotify)mm_bearer_ip_config_free);
            g_signal_connect (self,
                              "notify::ipv4-config",
                              G_CALLBACK (ipv4_config_changed),
                              NULL);
        }
    }

    return config;
}

MMBearerIpConfig *
mm_bearer_dup_ipv4_config (MMBearer *self)
{
    MMBearerIpConfig *config;
    GVariant *variant;

    g_return_val_if_fail (MM_IS_BEARER (self), NULL);

    variant = mm_gdbus_bearer_dup_ip4_config (MM_GDBUS_BEARER (self));
    config = create_ip_config_struct (variant);
    g_variant_unref (variant);

    return config;
}

#define MM_BEARER_IPV6_CONFIG_DATA "bearer-ipv6"

static void
ipv6_config_changed (MMBearer *self)
{
    g_object_set_data (G_OBJECT (self), MM_BEARER_IPV6_CONFIG_DATA, NULL);
}

const MMBearerIpConfig *
mm_bearer_get_ipv6_config (MMBearer *self)
{
    MMBearerIpConfig *config;

    g_return_val_if_fail (MM_IS_BEARER (self), NULL);

    config = g_object_get_data (G_OBJECT (self),
                                MM_BEARER_IPV6_CONFIG_DATA);
    if (!config) {
        config = create_ip_config_struct (
            mm_gdbus_bearer_get_ip6_config (MM_GDBUS_BEARER (self)));

        /* Note: if no specific method set, config struct will not be created */
        if (config) {
            g_object_set_data_full (G_OBJECT (self),
                                    MM_BEARER_IPV6_CONFIG_DATA,
                                    config,
                                    (GDestroyNotify)mm_bearer_ip_config_free);
            g_signal_connect (self,
                              "notify::ipv6-config",
                              G_CALLBACK (ipv6_config_changed),
                              NULL);
        }
    }

    return config;
}

MMBearerIpConfig *
mm_bearer_dup_ipv6_config (MMBearer *self)
{
    MMBearerIpConfig *config;
    GVariant *variant;

    g_return_val_if_fail (MM_IS_BEARER (self), NULL);

    variant = mm_gdbus_bearer_dup_ip6_config (MM_GDBUS_BEARER (self));
    config = create_ip_config_struct (variant);
    g_variant_unref (variant);

    return config;
}

/**
 * mm_bearer_connect:
 * @self: A #MMBearer.
 * @number: (allow-none): Number to use for dialing.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously requests activation of a packet data connection with the
 * network using this #MMBearer properties.
 * @number is usually not required. It is only required for POTS connections
 * or in certain special circumstances.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_bearer_connect_finish() to get the result of the operation.
 *
 * See mm_bearer_connect_sync() for the synchronous, blocking version of this method.
 */
void
mm_bearer_connect (MMBearer *self,
                   const gchar *number,
                   GCancellable *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_BEARER (self));

    mm_gdbus_bearer_call_connect (self,
                                  number,
                                  cancellable,
                                  callback,
                                  user_data);
}

/**
 * mm_bearer_connect_finish:
 * @self: A #MMBearer.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_bearer_connect().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_bearer_connect().
 *
 * Returns: (skip): %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_bearer_connect_finish (MMBearer *self,
                          GAsyncResult *res,
                          GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_call_connect_finish (self, res, error);
}

/**
 * mm_bearer_connect_sync:
 * @self: A #MMBearer.
 * @number: (allow-none): Number to use for dialing.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests activation of a packet data connection with the
 * network using this #MMBearer properties.
 * @number is usually not required. It is only required for POTS connections
 * or in certain special circumstances.
 *
 * The calling thread is blocked until a reply is received.
 * See mm_bearer_connect() for the asynchronous version of this method.
 *
 * Returns: (skip): %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_bearer_connect_sync (MMBearer *self,
                        const gchar *number,
                        GCancellable *cancellable,
                        GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_call_connect_sync (self,
                                              number,
                                              cancellable,
                                              error);
}

/**
 * mm_bearer_disconnect:
 * @self: A #MMBearer.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Synchronously requests disconnection and deactivation of the packet data connection.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_bearer_disconnect_finish() to get the result of the operation.
 *
 * See mm_bearer_disconnect_sync() for the synchronous, blocking version of this method.
 */
void
mm_bearer_disconnect (MMBearer *self,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_BEARER (self));

    mm_gdbus_bearer_call_disconnect (self,
                                     cancellable,
                                     callback,
                                     user_data);
}

/**
 * mm_bearer_disconnect_finish:
 * @self: A #MMBearer.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_bearer_disconnect().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_bearer_disconnect().
 *
 * Returns: (skip): %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_bearer_disconnect_finish (MMBearer *self,
                             GAsyncResult *res,
                             GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_call_disconnect_finish (self, res, error);
}

/**
 * mm_bearer_disconnect_sync:
 * @self: A #MMBearer.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously requests disconnection and deactivation of the packet data connection.
 *
 * The calling thread is blocked until a reply is received.
 * See mm_bearer_disconnect() for the asynchronous version of this method.
 *
 * Returns: (skip): %TRUE if the operation succeded, %FALSE if @error is set.
 */
gboolean
mm_bearer_disconnect_sync (MMBearer *self,
                           GCancellable *cancellable,
                           GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_BEARER (self), FALSE);

    return mm_gdbus_bearer_call_disconnect_sync (self,
                                                 cancellable,
                                                 error);
}