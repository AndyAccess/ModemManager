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

#include <gio/gio.h>

#include <mm-common-helpers.h>

#include "mm-helpers.h"
#include "mm-modem.h"

/**
 * mm_modem_get_path:
 * @self: A #MMModem.
 *
 * Gets the DBus path of the #MMObject which implements this interface.
 *
 * Returns: (transfer none): The DBus path of the #MMObject object.
 */
const gchar *
mm_modem_get_path (MMModem *self)
{
    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        g_dbus_proxy_get_object_path (G_DBUS_PROXY (self)));
}

/**
 * mm_modem_dup_path:
 * @self: A #MMModem.
 *
 * Gets a copy of the DBus path of the #MMObject object which implements this interface.
 *
 * Returns: (transfer full): The DBus path of the #MMObject. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_path (MMModem *self)
{
    gchar *value;

    g_return_val_if_fail (G_IS_DBUS_PROXY (self), NULL);

    g_object_get (G_OBJECT (self),
                  "g-object-path", &value,
                  NULL);

    RETURN_NON_EMPTY_STRING (value);
}

/**
 * mm_modem_get_sim_path:
 * @self: A #MMModem.
 *
 * Gets the DBus path of the #MMSim handled in this #MMModem.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_sim_path() if on another thread.</warning>
 *
 * Returns: (transfer none): The DBus path of the #MMSim handled in this #MMModem, or %NULL if none available.
 */
const gchar *
mm_modem_get_sim_path (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (mm_gdbus_modem_get_sim (self));
}

/**
 * mm_modem_dup_sim_path:
 * @self: A #MMModem.
 *
 * Gets a copy of the DBus path of the #MMSim handled in this #MMModem.
 *
 * Returns: (transfer full): The DBus path of the #MMSim handled in this #MMModem, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_sim_path (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_sim (self));
}

/**
 * mm_modem_get_modem_capabilities:
 * @self: A #MMModem.
 *
 * Gets the list of generic families of access technologies supported by this #MMModem.
 *
 * Not all capabilities are available at the same time however; some
 * modems require a firmware reload or other reinitialization to switch
 * between e.g. CDMA/EVDO and GSM/UMTS.
 *
 * Returns: A bitmask of #MMModemCapability flags.
 */
MMModemCapability
mm_modem_get_modem_capabilities (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), MM_MODEM_CAPABILITY_NONE);

    return (MMModemCapability) mm_gdbus_modem_get_modem_capabilities (self);
}

/**
 * mm_modem_get_current_capabilities:
 * @self: A #MMModem.
 *
 * Gets the list of generic families of access technologies supported by this #MMModem
 * without a firmware reload or reinitialization.
 *
 * Returns: A bitmask of #MMModemCapability flags.
 */
MMModemCapability
mm_modem_get_current_capabilities (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), MM_MODEM_CAPABILITY_NONE);

    return mm_gdbus_modem_get_current_capabilities (self);
}

/**
 * mm_modem_get_max_bearers:
 * @self: a #MMModem.
 *
 * Gets the maximum number of defined packet data bearers this #MMModem supports.
 *
 * This is not the number of active/connected bearers the modem supports,
 * but simply the number of bearers that may be defined at any given time.
 * For example, POTS and CDMA2000-only devices support only one bearer,
 * while GSM/UMTS devices typically support three or more, and any
 * LTE-capable device (whether LTE-only, GSM/UMTS-capable, and/or
 * CDMA2000-capable) also typically support three or more.
 *
 * Returns: the maximum number of defined packet data bearers.
 */
guint
mm_modem_get_max_bearers (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), 0);

    return mm_gdbus_modem_get_max_bearers (self);
}

/**
 * mm_modem_get_max_bearers:
 * @self: a #MMModem.
 *
 * Gets the maximum number of active packet data bearers this #MMModem supports.
 *
 * POTS and CDMA2000-only devices support one active bearer, while GSM/UMTS
 * and LTE-capable devices (including LTE/CDMA devices) typically support
 * at least two active bearers.
 *
 * Returns: the maximum number of defined packet data bearers.
 */
guint
mm_modem_get_max_active_bearers (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), 0);

    return mm_gdbus_modem_get_max_active_bearers (self);
}

/**
 * mm_modem_get_manufacturer:
 * @self: A #MMModem.
 *
 * Gets the equipment manufacturer, as reported by this #MMModem.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_manufacturer() if on another thread.</warning>
 *
 * Returns: (transfer none): The equipment manufacturer, or %NULL if none available.
 */
const gchar *
mm_modem_get_manufacturer (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_manufacturer (self));
}

/**
 * mm_modem_dup_manufacturer:
 * @self: A #MMModem.
 *
 * Gets a copy of the equipment manufacturer, as reported by this #MMModem.
 *
 * Returns: (transfer full): The equipment manufacturer, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_manufacturer (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_manufacturer (self));
}

/**
 * mm_modem_get_model:
 * @self: A #MMModem.
 *
 * Gets the equipment model, as reported by this #MMModem.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_model() if on another thread.</warning>
 *
 * Returns: (transfer none): The equipment model, or %NULL if none available.
 */
const gchar *
mm_modem_get_model (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_model (self));
}

/**
 * mm_modem_dup_model:
 * @self: A #MMModem.
 *
 * Gets a copy of the equipment model, as reported by this #MMModem.
 *
 * Returns: (transfer full): The equipment model, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_model (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_model (self));
}

/**
 * mm_modem_get_revision:
 * @self: A #MMModem.
 *
 * Gets the equipment revision, as reported by this #MMModem.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_revision() if on another thread.</warning>
 *
 * Returns: (transfer none): The equipment revision, or %NULL if none available.
 */
const gchar *
mm_modem_get_revision (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_revision (self));
}

/**
 * mm_modem_dup_revision:
 * @self: A #MMModem.
 *
 * Gets a copy of the equipment revision, as reported by this #MMModem.
 *
 * Returns: (transfer full): The equipment revision, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_revision (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_revision (self));
}

/**
 * mm_modem_get_device_identifier:
 * @self: A #MMModem.
 *
 * Gets a best-effort device identifier based on various device information like
 * model name, firmware revision, USB/PCI/PCMCIA IDs, and other properties.
 *
 * This ID is not guaranteed to be unique and may be shared between
 * identical devices with the same firmware, but is intended to be "unique
 * enough" for use as a casual device identifier for various user
 * experience operations.
 *
 * This is not the device's IMEI or ESN since those may not be available
 * before unlocking the device via a PIN.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_device_identifier() if on another thread.</warning>
 *
 * Returns: (transfer none): The device identifier, or %NULL if none available.
 */
const gchar *
mm_modem_get_device_identifier (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_device_identifier (self));
}

/**
 * mm_modem_dup_device_identifier:
 * @self: A #MMModem.
 *
 * Gets a copy of a best-effort device identifier based on various device information
 * like model name, firmware revision, USB/PCI/PCMCIA IDs, and other properties.
 *
 * This ID is not guaranteed to be unique and may be shared between
 * identical devices with the same firmware, but is intended to be "unique
 * enough" for use as a casual device identifier for various user
 * experience operations.
 *
 * This is not the device's IMEI or ESN since those may not be available
 * before unlocking the device via a PIN.
 *
 * Returns: (transfer full): The device identifier, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_device_identifier (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_device_identifier (self));
}

/**
 * mm_modem_get_device:
 * @self: A #MMModem.
 *
 * Gets the physical modem device reference (ie, USB, PCI, PCMCIA device), which
 * may be dependent upon the operating system.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_device() if on another thread.</warning>
 *
 * Returns: (transfer none): The device, or %NULL if none available.
 */
const gchar *
mm_modem_get_device (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_device (self));
}

/**
 * mm_modem_dup_device:
 * @self: A #MMModem.
 *
 * Gets a copy of the physical modem device reference (ie, USB, PCI, PCMCIA device), which
 * may be dependent upon the operating system.
 *
 * Returns: (transfer full): The device, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_device (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_device (self));
}

/**
 * mm_modem_get_driver:
 * @self: A #MMModem.
 *
 * Gets the Operating System device driver handling communication with the modem
 * hardware.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_driver() if on another thread.</warning>
 *
 * Returns: (transfer none): The driver, or %NULL if none available.
 */
const gchar *
mm_modem_get_driver (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_driver (self));
}

/**
 * mm_modem_dup_driver:
 * @self: A #MMModem.
 *
 * Gets a copy of the Operating System device driver handling communication with the modem
 * hardware.
 *
 * Returns: (transfer full): The driver, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_driver (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_driver (self));
}

/**
 * mm_modem_get_plugin:
 * @self: A #MMModem.
 *
 * Gets the name of the plugin handling this #MMModem.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_plugin() if on another thread.</warning>
 *
 * Returns: (transfer none): The name of the plugin, or %NULL if none available.
 */
const gchar *
mm_modem_get_plugin (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_plugin (self));
}

/**
 * mm_modem_dup_plugin:
 * @self: A #MMModem.
 *
 * Gets a copy of the name of the plugin handling this #MMModem.
 *
 * Returns: (transfer full): The name of the plugin, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_plugin (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_plugin (self));
}

/**
 * mm_modem_get_equipment_identifier:
 * @self: A #MMModem.
 *
 * Gets the identity of the #MMModem.
 *
 * This will be the IMEI number for GSM devices and the hex-format ESN/MEID
 * for CDMA devices.
 *
 * <warning>It is only safe to use this function on the thread where @self was constructed. Use mm_modem_dup_equipment_identifier() if on another thread.</warning>
 *
 * Returns: (transfer none): The equipment identifier, or %NULL if none available.
 */
const gchar *
mm_modem_get_equipment_identifier (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_CONSTANT_STRING (
        mm_gdbus_modem_get_equipment_identifier (self));
}

/**
 * mm_modem_dup_equipment_identifier:
 * @self: A #MMModem.
 *
 * Gets a copy of the identity of the #MMModem.
 *
 * This will be the IMEI number for GSM devices and the hex-format ESN/MEID
 * for CDMA devices.
 *
 * Returns: (transfer full): The equipment identifier, or %NULL if none available. The returned value should be freed with g_free().
 */
gchar *
mm_modem_dup_equipment_identifier (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    RETURN_NON_EMPTY_STRING (
        mm_gdbus_modem_dup_equipment_identifier (self));
}

/**
 * mm_modem_get_unlock_required:
 * @self: A #MMModem.
 *
 * Gets current lock state of the #MMModemm.
 *
 * Returns: A #MMModemLock value, specifying the current lock state.
 */
MMModemLock
mm_modem_get_unlock_required (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), MM_MODEM_LOCK_UNKNOWN);

    return (MMModemLock) mm_gdbus_modem_get_unlock_required (self);
}

/**
 * mm_modem_get_unlock_retries:
 * @self: A #MMModem.
 *
 * Gets the number of unlock retries remaining for the lock code given by the
 * UnlockRequired property (if any), or 999 if the device does not support reporting
 * unlock retries.
 *
 * Returns: The number of unlock retries.
 */
guint
mm_modem_get_unlock_retries (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), 0);

    return mm_gdbus_modem_get_unlock_retries (self);
}

/**
 * mm_modem_get_state:
 * @self: A #MMModem.
 *
 * Gets the overall state of the #MMModem.
 *
 * Returns: A #MMModemState value.
 */
MMModemState
mm_modem_get_state (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), MM_MODEM_STATE_UNKNOWN);

    return (MMModemState) mm_gdbus_modem_get_state (self);
}

/**
 * mm_modem_get_access_technology:
 * @self: A #MMModem.
 *
 * Gets the current network access technology used by the #MMModem to communicate
 * with the network.
 *
 * Returns: A ##MMModemAccessTech value.
 */
MMModemAccessTech
mm_modem_get_access_technology (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), MM_MODEM_ACCESS_TECH_UNKNOWN);

    return (MMModemAccessTech) mm_gdbus_modem_get_access_technology (self);
}

/**
 * mm_modem_get_signal_quality:
 * @self: A #MMModem.
 * @recent: (out): Return location for the flag specifying if the signal quality value was recent or not.
 *
 * Gets the signal quality value in percent (0 - 100) of the dominant access technology
 * the #MMModem is using to communicate with the network.
 *
 * Always 0 for POTS devices.
 *
 * Returns: The signal quality.
 */
guint
mm_modem_get_signal_quality (MMModem *self,
                             gboolean *recent)
{
    GVariant *variant;
    gboolean is_recent = FALSE;
    guint quality = 0;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), 0);

    variant = mm_gdbus_modem_get_signal_quality (self);
    if (variant) {
        g_variant_get (variant,
                       "(ub)",
                       &quality,
                       &is_recent);
    }

    if (recent)
        *recent = is_recent;
    return quality;
}

/**
 * mm_modem_get_supported_modes:
 * @self: A #MMModem.
 *
 * Gets the list of modes specifying the access technologies supported by the #MMModem.
 *
 * For POTS devices, only #MM_MODEM_MODE_ANY will be returned.
 *
 * Returns: A bitmask of #MMModemMode values.
 */
MMModemMode
mm_modem_get_supported_modes (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), MM_MODEM_MODE_NONE);

    return (MMModemMode) mm_gdbus_modem_get_supported_modes (self);
}

/**
 * mm_modem_get_allowed_modes:
 * @self: A #MMModem.
 *
 * Gets the list of modes specifying the access technologies (eg 2G/3G/4G preference)
 * the #MMModem is currently allowed to use when connecting to a network.
 *
 * For POTS devices, only the #MM_MODEM_MODE_ANY is supported.
 *
 * Returns: A bitmask of #MMModemMode values.
 */
MMModemMode
mm_modem_get_allowed_modes (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), MM_MODEM_MODE_NONE);

    return (MMModemMode) mm_gdbus_modem_get_allowed_modes (self);
}

/**
 * mm_modem_get_preferred_mode:
 * @self: A #MMModem.
 *
 * Get the preferred access technology (eg 2G/3G/4G preference), among
 * the ones defined in the allowed modes.
 *
 * Returns: A single #MMModemMode value.
 */
MMModemMode
mm_modem_get_preferred_mode (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), MM_MODEM_MODE_NONE);

    return (MMModemMode) mm_gdbus_modem_get_preferred_mode (self);
}

/**
 * mm_modem_get_supported_bands:
 * @self: A #MMModem.
 *
 * Gets the list of radio frequency and technology bands supported by the #MMModem.
 *
 * For POTS devices, only #MM_MODEM_BAND_ANY will be returned.
 *
 * Returns: A bitmask of #MMModemBand values.
 */
MMModemBand
mm_modem_get_supported_bands (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), MM_MODEM_MODE_NONE);

    return (MMModemBand) mm_gdbus_modem_get_supported_bands (self);
}

/**
 * mm_modem_get_allowed_bands:
 * @self: A #MMModem.
 *
 * Gets the list of radio frequency and technology bands the #MMModem is currently
 * allowed to use when connecting to a network.
 *
 * For POTS devices, only the #MM_MODEM_BAND_ANY band is supported.
 *
 * Returns: A bitmask of #MMModemBand values.
 */
MMModemBand
mm_modem_get_allowed_bands (MMModem *self)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), MM_MODEM_MODE_NONE);

    return (MMModemBand) mm_gdbus_modem_get_allowed_bands (self);
}

/**
 * mm_modem_enable:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously tries to enable the #MMModem. When enabled, the modem's radio is
 * powered on and data sessions, voice calls, location services, and Short Message
 * Service may be available.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_enable_finish() to get the result of the operation.
 *
 * See mm_modem_enable_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_enable (MMModem *self,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM (self));

    mm_gdbus_modem_call_enable (self,
                                TRUE,
                                cancellable,
                                callback,
                                user_data);
}

/**
 * mm_modem_enable_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_enable().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_enable().
 *
 * Returns: (skip): %TRUE if the modem was properly enabled, %FALSE if @error is set.
 */
gboolean
mm_modem_enable_finish (MMModem *self,
                        GAsyncResult *res,
                        GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_enable_finish (self, res, error);
}

/**
 * mm_modem_enable_sync:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously tries to enable the #MMModem. When enabled, the modem's radio is
 * powered on and data sessions, voice calls, location services, and Short Message
 * Service may be available.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_enable()
 * for the asynchronous version of this method.
 *
 * Returns: (skip): %TRUE if the modem was properly enabled, %FALSE if @error is set.
 */
gboolean
mm_modem_enable_sync (MMModem *self,
                      GCancellable *cancellable,
                      GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_enable_sync (self,
                                            TRUE,
                                            cancellable,
                                            error);
}

/**
 * mm_modem_disable:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously tries to disable the #MMModem. When disabled, the modem enters
 * low-power state and no network-related operations are available.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_disable_finish() to get the result of the operation.
 *
 * See mm_modem_disable_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_disable (MMModem *self,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM (self));

    mm_gdbus_modem_call_enable (self,
                                FALSE,
                                cancellable,
                                callback,
                                user_data);
}

/**
 * mm_modem_disable_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_disable().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_disable().
 *
 * Returns: (skip): %TRUE if the modem was properly disabled, %FALSE if @error is set.
 */
gboolean
mm_modem_disable_finish (MMModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_enable_finish (self, res, error);
}

/**
 * mm_modem_disable_sync:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously tries to disable the #MMModem. When disabled, the modem enters
 * low-power state and no network-related operations are available.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_disable()
 * for the asynchronous version of this method.
 *
 * Returns: (skip): %TRUE if the modem was properly disabled, %FALSE if @error is set.
 */
gboolean
mm_modem_disable_sync (MMModem *self,
                      GCancellable *cancellable,
                      GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_enable_sync (self,
                                            FALSE,
                                            cancellable,
                                            error);
}

/**
 * mm_modem_list_bearers:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously lists the packet data bearers in the #MMModem.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_list_bearers_finish() to get the result of the operation.
 *
 * See mm_modem_list_bearers_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_list_bearers (MMModem *self,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM (self));

    mm_gdbus_modem_call_list_bearers (self,
                                      cancellable,
                                      callback,
                                      user_data);
}

/**
 * mm_modem_list_bearers_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_list_bearers().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_list_bearers().
 *
 * Returns: (transfer-full): The list of bearer object paths, or %NULL if either none found or if @error is set.
 */
gchar **
mm_modem_list_bearers_finish (MMModem *self,
                              GAsyncResult *res,
                              GError **error)
{
    gchar **list;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), FALSE);

    if (!mm_gdbus_modem_call_list_bearers_finish (self,
                                                  &list,
                                                  res,
                                                  error))
        return NULL;

    /* Only non-empty lists are returned */
    if (list && list[0])
        return list;

    g_strfreev (list);
    return NULL;
}

/**
 * mm_modem_list_bearers_sync:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 *
 * Synchronously lists the packet data bearers in the #MMModem.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_list_bearers()
 * for the asynchronous version of this method.
 *
 * Returns: (transfer-full): The list of bearer object paths, or %NULL if either none found or if @error is set.
 */
gchar **
mm_modem_list_bearers_sync (MMModem *self,
                            GCancellable *cancellable,
                            GError **error)
{
    gchar **list;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), FALSE);

    if (!mm_gdbus_modem_call_list_bearers_sync (self,
                                                &list,
                                                cancellable,
                                                error))
        return NULL;

    /* Only non-empty lists are returned */
    if (list && list[0])
        return list;

    g_strfreev (list);
    return NULL;
}

static GVariant *
create_bearer_build_properties (const gchar *first_property_name,
                                va_list var_args)
{
    const gchar *key;
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{ss}"));

    key = first_property_name;
    while (key) {
        const gchar *value;

        /* If a key with NULL value is given, just ignore it. */
        value = va_arg (var_args, gchar *);
        if (value)
            g_variant_builder_add (&builder, "{ss}", key, value);

        key = va_arg (var_args, gchar *);
    }
    return g_variant_builder_end (&builder);
}

/**
 * mm_modem_create_bearer:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 * @first_property_name: Name of the first property to set.
 * @...: Value for the first property, followed optionally by more name/value pairs, followed by %NULL.
 *
 * Asynchronously creates a new packet data bearer in the #MMModem.
 *
 * This request may fail if the modem does not support additional bearers,
 * if too many bearers are already defined, or if @properties are invalid.
 *
 * See <link linkend="gdbus-method-org-freedesktop-ModemManager1-Modem.CreateBearer">CreateBearer</link> to check which properties may be passed.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_create_bearer_finish() to get the result of the operation.
 *
 * See mm_modem_create_bearer_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_create_bearer (MMModem *self,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data,
                        const gchar *first_property_name,
                        ...)
{
    va_list va_args;
    GVariant *properties;

    g_return_if_fail (MM_GDBUS_IS_MODEM (self));

    va_start (va_args, first_property_name);
    properties = create_bearer_build_properties (first_property_name, va_args);

    mm_gdbus_modem_call_create_bearer (self,
                                       properties,
                                       cancellable,
                                       callback,
                                       user_data);

    g_variant_unref (properties);
    va_end (va_args);
}

/**
 * mm_modem_create_bearer_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_create_bearer().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_create_bearer().
 *
 * Returns: (transfer-full): Path of the newly created bearer, or %NULL if @error is set.
 */
gchar *
mm_modem_create_bearer_finish (MMModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    gchar *out_path = NULL;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    if (!mm_gdbus_modem_call_create_bearer_finish (self,
                                                   &out_path,
                                                   res,
                                                   error))
        return NULL;

    return out_path;
}

/**
 * mm_modem_create_bearer_sync:
 * @self: A #MMModem.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.
 * @first_property_name: Name of the first property to set.
 * @...: Value for the first property, followed optionally by more name/value pairs, followed by %NULL.
 *
 * Synchronously creates a new packet data bearer in the #MMModem.
 *
 * This request may fail if the modem does not support additional bearers,
 * if too many bearers are already defined, or if @properties are invalid.
 *
 * See <link linkend="gdbus-method-org-freedesktop-ModemManager1-Modem.CreateBearer">CreateBearer</link> to check which properties may be passed.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_create_bearer()
 * for the asynchronous version of this method.
 *
 * Returns: (transfer-full): Path of the newly created bearer, or %NULL if @error is set.
 */
gchar *
mm_modem_create_bearer_sync (MMModem *self,
                             GCancellable *cancellable,
                             GError **error,
                             const gchar *first_property_name,
                             ...)
{
    va_list va_args;
    GVariant *properties;
    gchar *out_path = NULL;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    va_start (va_args, first_property_name);
    properties = create_bearer_build_properties (first_property_name, va_args);

    mm_gdbus_modem_call_create_bearer_sync (self,
                                            properties,
                                            &out_path,
                                            cancellable,
                                            error);

    g_variant_unref (properties);
    va_end (va_args);

    return out_path;
}

/**
 * mm_modem_delete_bearer:
 * @self: A #MMModem.
 * @bearer: Path of the bearer to delete.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied or %NULL.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously deletes a given bearer from the #MMModem.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call mm_modem_delete_bearer_finish() to get the result of the operation.
 *
 * See mm_modem_delete_bearer_sync() for the synchronous, blocking version of this method.
 */
void
mm_modem_delete_bearer (MMModem *self,
                        const gchar *bearer,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM (self));

    mm_gdbus_modem_call_delete_bearer (self,
                                       bearer,
                                       cancellable,
                                       callback,
                                       user_data);
}

/**
 * mm_modem_delete_bearer_finish:
 * @self: A #MMModem.
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to mm_modem_delete_bearer().
 * @error: Return location for error or %NULL.
 *
 * Finishes an operation started with mm_modem_delete_bearer().
 *
 * Returns: (skip): %TRUE if the bearer was deleted, %FALSE if @error is set.
 */
gboolean
mm_modem_delete_bearer_finish (MMModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_delete_bearer_finish (self,
                                                     res,
                                                     error);
}

/**
 * mm_modem_delete_bearer_sync:
 * @self: A #MMModem.
 * @bearer: Path of the bearer to delete.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL.

 * Synchronously deletes a given bearer from the #MMModem.
 *
 * The calling thread is blocked until a reply is received. See mm_modem_delete_bearer()
 * for the asynchronous version of this method.
 *
 * Returns: (skip): %TRUE if the bearer was deleted, %FALSE if @error is set.
 */
gboolean
mm_modem_delete_bearer_sync (MMModem *self,
                             const gchar *bearer,
                             GCancellable *cancellable,
                             GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_delete_bearer_sync (self,
                                                   bearer,
                                                   cancellable,
                                                   error);
}

void
mm_modem_reset (MMModem *self,
                GCancellable *cancellable,
                GAsyncReadyCallback callback,
                gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM (self));

    mm_gdbus_modem_call_reset (self,
                               cancellable,
                               callback,
                               user_data);
}

gboolean
mm_modem_reset_finish (MMModem *self,
                       GAsyncResult *res,
                       GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_reset_finish (self,
                                             res,
                                             error);
}

gboolean
mm_modem_reset_sync (MMModem *self,
                     GCancellable *cancellable,
                     GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_reset_sync (self,
                                           cancellable,
                                           error);
}

void
mm_modem_factory_reset (MMModem *self,
                        const gchar *code,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM (self));

    mm_gdbus_modem_call_factory_reset (self,
                                       code,
                                       cancellable,
                                       callback,
                                       user_data);
}

gboolean
mm_modem_factory_reset_finish (MMModem *self,
                               GAsyncResult *res,
                               GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_factory_reset_finish (self,
                                                     res,
                                                     error);
}

gboolean
mm_modem_factory_reset_sync (MMModem *self,
                             const gchar *code,
                             GCancellable *cancellable,
                             GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_factory_reset_sync (self,
                                                   code,
                                                   cancellable,
                                                   error);
}

gboolean
mm_modem_set_allowed_modes_finish (MMModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_set_allowed_modes_finish (self,
                                                         res,
                                                         error);
}

void
mm_modem_set_allowed_modes (MMModem *self,
                            MMModemMode modes,
                            MMModemMode preferred,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM (self));

    mm_gdbus_modem_call_set_allowed_modes (self,
                                           modes,
                                           preferred,
                                           cancellable,
                                           callback,
                                           user_data);
}

gboolean
mm_modem_set_allowed_modes_sync (MMModem *self,
                                 MMModemMode modes,
                                 MMModemMode preferred,
                                 GCancellable *cancellable,
                                 GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_set_allowed_modes_sync (self,
                                                       modes,
                                                       preferred,
                                                       cancellable,
                                                       error);
}

gboolean
mm_modem_set_allowed_bands_finish (MMModem *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_set_allowed_bands_finish (self,
                                                         res,
                                                         error);
}

void
mm_modem_set_allowed_bands (MMModem *self,
                            MMModemBand bands,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    g_return_if_fail (MM_GDBUS_IS_MODEM (self));

    mm_gdbus_modem_call_set_allowed_bands (self,
                                           bands,
                                           cancellable,
                                           callback,
                                           user_data);
}

gboolean
mm_modem_set_allowed_bands_sync (MMModem *self,
                                 MMModemBand bands,
                                 GCancellable *cancellable,
                                 GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), FALSE);

    return mm_gdbus_modem_call_set_allowed_bands_sync (self,
                                                       bands,
                                                       cancellable,
                                                       error);
}

static void
modem_get_sim_ready (GDBusConnection *connection,
                     GAsyncResult *res,
                     GSimpleAsyncResult *simple)
{
    GError *error = NULL;
    MMSim *sim;

    sim = mm_gdbus_sim_proxy_new_finish (res, &error);
    if (error)
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gpointer (simple, sim, NULL);

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_modem_get_sim (MMModem *self,
                  GCancellable *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer user_data)
{
    GSimpleAsyncResult *result;
    const gchar *sim_path;

    g_return_if_fail (MM_GDBUS_IS_MODEM (self));

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_modem_get_sim);

    sim_path = mm_modem_get_sim_path (self);
    if (!sim_path) {
        g_simple_async_result_set_op_res_gpointer (result, NULL, NULL);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    mm_gdbus_sim_proxy_new (
        g_dbus_proxy_get_connection (
            G_DBUS_PROXY (self)),
        G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
        MM_DBUS_SERVICE,
        sim_path,
        cancellable,
        (GAsyncReadyCallback)modem_get_sim_ready,
        result);
}

MMSim *
mm_modem_get_sim_finish (MMModem *self,
                         GAsyncResult *res,
                         GError **error)
{
    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error))
        return NULL;

    return g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (res));
}

MMSim *
mm_modem_get_sim_sync (MMModem *self,
                       GCancellable *cancellable,
                       GError **error)
{
    const gchar *sim_path;

    g_return_val_if_fail (MM_GDBUS_IS_MODEM (self), NULL);

    sim_path = mm_modem_get_sim_path (self);
    if (!sim_path)
        return NULL;

    return (mm_gdbus_sim_proxy_new_sync (
                g_dbus_proxy_get_connection (
                    G_DBUS_PROXY (self)),
                G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                MM_DBUS_SERVICE,
                sim_path,
                cancellable,
                error));
}

/**
 * mm_modem_get_capabilities_string:
 * @caps: Bitmask of #MMModemCapability flags.
 *
 * Build a string with a list of capabilities.
 *
 * Returns: (transfer full): A string specifying the capabilities given in @caps. The returned value should be freed with g_free().
 */
gchar *
mm_modem_get_capabilities_string (MMModemCapability caps)
{
    return mm_common_get_capabilities_string (caps);
}
