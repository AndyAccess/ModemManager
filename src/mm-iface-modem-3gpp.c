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
#include <libmm-common.h>

#include "mm-iface-modem.h"
#include "mm-iface-modem-3gpp.h"
#include "mm-bearer-3gpp.h"
#include "mm-bearer-list.h"
#include "mm-base-modem.h"
#include "mm-modem-helpers.h"
#include "mm-log.h"

#define REGISTRATION_CHECK_TIMEOUT_SEC 30

#define SUBSYSTEM_3GPP "3gpp"

#define INDICATORS_CHECKED_TAG            "3gpp-indicators-checked-tag"
#define UNSOLICITED_EVENTS_SUPPORTED_TAG  "3gpp-unsolicited-events-supported-tag"
#define REGISTRATION_STATE_CONTEXT_TAG    "3gpp-registration-state-context-tag"
#define REGISTRATION_CHECK_CONTEXT_TAG    "3gpp-registration-check-context-tag"

static GQuark indicators_checked_quark;
static GQuark unsolicited_events_supported_quark;
static GQuark registration_state_context_quark;
static GQuark registration_check_context_quark;

/*****************************************************************************/

void
mm_iface_modem_3gpp_bind_simple_status (MMIfaceModem3gpp *self,
                                        MMCommonSimpleProperties *status)
{
    MmGdbusModem3gpp *skeleton;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);

    g_object_bind_property (skeleton, "registration-state",
                            status, MM_COMMON_SIMPLE_PROPERTY_3GPP_REGISTRATION_STATE,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "operator-code",
                            status, MM_COMMON_SIMPLE_PROPERTY_3GPP_OPERATOR_CODE,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_bind_property (skeleton, "operator-name",
                            status, MM_COMMON_SIMPLE_PROPERTY_3GPP_OPERATOR_NAME,
                            G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

    g_object_unref (skeleton);
}

/*****************************************************************************/

typedef struct {
    MmGdbusModem3gpp *skeleton;
    GDBusMethodInvocation *invocation;
    MMIfaceModem3gpp *self;
} DbusCallContext;

static void
dbus_call_context_free (DbusCallContext *ctx)
{
    g_object_unref (ctx->skeleton);
    g_object_unref (ctx->invocation);
    g_object_unref (ctx->self);
    g_free (ctx);
}

static DbusCallContext *
dbus_call_context_new (MmGdbusModem3gpp *skeleton,
                       GDBusMethodInvocation *invocation,
                       MMIfaceModem3gpp *self)
{
    DbusCallContext *ctx;

    ctx = g_new (DbusCallContext, 1);
    ctx->skeleton = g_object_ref (skeleton);
    ctx->invocation = g_object_ref (invocation);
    ctx->self = g_object_ref (self);
    return ctx;
}

/*****************************************************************************/

gboolean
mm_iface_modem_3gpp_register_in_network_finish (MMIfaceModem3gpp *self,
                                                GAsyncResult *res,
                                                GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
register_in_network_ready (MMIfaceModem3gpp *self,
                           GAsyncResult *res,
                           GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network_finish (self, res, &error))
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);
    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

void
mm_iface_modem_3gpp_register_in_network (MMIfaceModem3gpp *self,
                                         const gchar *operator_id,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    GSimpleAsyncResult *result;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_iface_modem_3gpp_register_in_network);
    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network (
        self,
        operator_id,
        (GAsyncReadyCallback)register_in_network_ready,
        result);
}

static void
handle_register_ready (MMIfaceModem3gpp *self,
                       GAsyncResult *res,
                       DbusCallContext *ctx)
{
    GError *error = NULL;

    if (!MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network_finish (self,
                                                                               res,
                                                                               &error))
        g_dbus_method_invocation_take_error (ctx->invocation,
                                             error);
    else
        mm_gdbus_modem3gpp_complete_register (ctx->skeleton,
                                               ctx->invocation);
    dbus_call_context_free (ctx);
}

static gboolean
handle_register (MmGdbusModem3gpp *skeleton,
                 GDBusMethodInvocation *invocation,
                 const gchar *network_id,
                 MMIfaceModem3gpp *self)
{
    MMModemState modem_state;

    g_assert (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network != NULL);
    g_assert (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->register_in_network_finish != NULL);

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_UNKNOWN:
        /* We should never have a UNKNOWN->REGISTERED transition */
        g_assert_not_reached ();
        break;

    case MM_MODEM_STATE_LOCKED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot register modem: device locked");
        break;

    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
        mm_iface_modem_3gpp_register_in_network (self,
                                                 network_id,
                                                 (GAsyncReadyCallback)handle_register_ready,
                                                 dbus_call_context_new (skeleton,
                                                                        invocation,
                                                                        self));
        break;

    case MM_MODEM_STATE_DISABLING:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot register modem: "
                                               "currently being disabled");
        break;

    case MM_MODEM_STATE_ENABLING:
    case MM_MODEM_STATE_DISABLED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot register modem: "
                                               "not yet enabled");
        break;

    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot register modem: "
                                               "modem is connected");
        break;
    }

    return TRUE;
}

/*****************************************************************************/

static GVariant *
scan_networks_build_result (GList *info_list)
{
    GList *l;
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

    for (l = info_list; l; l = g_list_next (l)) {
        MM3gppNetworkInfo *info = l->data;

        if (!info->operator_code) {
            g_warn_if_reached ();
            continue;
        }

        g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));

        g_variant_builder_add (&builder, "{sv}",
                               "operator-code", g_variant_new_string (info->operator_code));
        g_variant_builder_add (&builder, "{sv}",
                               "status", g_variant_new_uint32 (info->status));
        g_variant_builder_add (&builder, "{sv}",
                               "access-technology", g_variant_new_uint32 (info->access_tech));
        if (info->operator_long)
            g_variant_builder_add (&builder, "{sv}",
                                   "operator-long", g_variant_new_string (info->operator_long));
        if (info->operator_short)
            g_variant_builder_add (&builder, "{sv}",
                                   "operator-short", g_variant_new_string (info->operator_short));
        g_variant_builder_close (&builder);
    }

    return g_variant_builder_end (&builder);
}

static void
scan_networks_ready (MMIfaceModem3gpp *self,
                     GAsyncResult *res,
                     DbusCallContext *ctx)
{
    GError *error = NULL;
    GList *info_list;

    info_list = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->scan_networks_finish (self,
                                                                                res,
                                                                                &error);
    if (error)
        g_dbus_method_invocation_take_error (ctx->invocation,
                                             error);
    else {
        GVariant *dict_array;

        dict_array = scan_networks_build_result (info_list);
        mm_gdbus_modem3gpp_complete_scan (ctx->skeleton,
                                          ctx->invocation,
                                          dict_array);
        g_variant_unref (dict_array);
    }

    mm_3gpp_network_info_list_free (info_list);
    dbus_call_context_free (ctx);
}

static gboolean
handle_scan (MmGdbusModem3gpp *skeleton,
             GDBusMethodInvocation *invocation,
             MMIfaceModem3gpp *self)
{

    MMModemState modem_state;

    /* If scanning is not implemented, report an error */
    if (!MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->scan_networks ||
        !MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->scan_networks_finish) {
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_UNSUPPORTED,
                                               "Cannot scan networks: operation not supported");
        return TRUE;
    }

    modem_state = MM_MODEM_STATE_UNKNOWN;
    g_object_get (self,
                  MM_IFACE_MODEM_STATE, &modem_state,
                  NULL);

    switch (modem_state) {
    case MM_MODEM_STATE_UNKNOWN:
        /* We should never have such request in UNKNOWN state */
        g_assert_not_reached ();
        break;

    case MM_MODEM_STATE_LOCKED:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot scan networks: device locked");
        break;

    case MM_MODEM_STATE_DISABLED:
    case MM_MODEM_STATE_DISABLING:
    case MM_MODEM_STATE_ENABLING:
        g_dbus_method_invocation_return_error (invocation,
                                               MM_CORE_ERROR,
                                               MM_CORE_ERROR_WRONG_STATE,
                                               "Cannot scan networks: not enabled yet");
        break;

    case MM_MODEM_STATE_ENABLED:
    case MM_MODEM_STATE_SEARCHING:
    case MM_MODEM_STATE_REGISTERED:
    case MM_MODEM_STATE_DISCONNECTING:
    case MM_MODEM_STATE_CONNECTING:
    case MM_MODEM_STATE_CONNECTED:
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->scan_networks (
            self,
            (GAsyncReadyCallback)scan_networks_ready,
            dbus_call_context_new (skeleton,
                                   invocation,
                                   self));
        break;
    }

    return TRUE;
}

/*****************************************************************************/

/* Create new 3GPP bearer */
MMBearer *
mm_iface_modem_3gpp_create_bearer (MMIfaceModem3gpp *self,
                                   MMCommonBearerProperties *properties,
                                   GError **error)
{
    MMModem3gppRegistrationState current_state;
    MMBearer *bearer;

    g_assert (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->create_3gpp_bearer != NULL);

    /* Create new 3GPP bearer using the method set in the interface, so that
     * plugins can subclass it and implement their own. */
    bearer = MM_BEARER (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->create_3gpp_bearer (
                            MM_BASE_MODEM (self),
                            properties,
                            error));
    if (!bearer)
        return NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &current_state,
                  NULL);

    /* Don't allow bearer to get connected if roaming forbidden */
    if (current_state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME)
        mm_bearer_set_connection_allowed (bearer);
    else if (current_state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING) {
        if (mm_bearer_3gpp_get_allow_roaming (MM_BEARER_3GPP (bearer)))
            mm_bearer_set_connection_allowed (bearer);
        else
            mm_bearer_set_connection_forbidden (
                bearer,
                MM_BEARER_CONNECTION_FORBIDDEN_REASON_ROAMING);
    }
    else
        mm_bearer_set_connection_forbidden (
            bearer,
            MM_BEARER_CONNECTION_FORBIDDEN_REASON_UNREGISTERED);

    return bearer;
}

/*****************************************************************************/

typedef struct {
    GSimpleAsyncResult *result;
    gboolean cs_supported;
    gboolean ps_supported;
    gboolean cs_done;
    GError *cs_reg_error;
    GError *ps_reg_error;
} RunAllRegistrationChecksContext;

static void
run_all_registration_checks_context_complete_and_free (RunAllRegistrationChecksContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_clear_error (&ctx->cs_reg_error);
    g_clear_error (&ctx->ps_reg_error);
    g_object_unref (ctx->result);
    g_free (ctx);
}

gboolean
mm_iface_modem_3gpp_run_all_registration_checks_finish (MMIfaceModem3gpp *self,
                                                        GAsyncResult *res,
                                                        GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
run_ps_registration_check_ready (MMIfaceModem3gpp *self,
                                 GAsyncResult *res,
                                 RunAllRegistrationChecksContext *ctx)
{
    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check_finish (self, res, &ctx->ps_reg_error);

    /* If both CS and PS registration checks returned errors we fail */
    if (ctx->ps_reg_error &&
        (ctx->cs_reg_error || !ctx->cs_done))
        /* Prefer the PS error */
        g_simple_async_result_set_from_error (ctx->result, ctx->ps_reg_error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    run_all_registration_checks_context_complete_and_free (ctx);
}

static void
run_cs_registration_check_ready (MMIfaceModem3gpp *self,
                                 GAsyncResult *res,
                                 RunAllRegistrationChecksContext *ctx)
{
    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_cs_registration_check_finish (self, res, &ctx->cs_reg_error);

    if (ctx->ps_supported &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check_finish) {
        ctx->cs_done = TRUE;
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check (
            self,
            (GAsyncReadyCallback)run_ps_registration_check_ready,
            ctx);
        return;
    }

    /* All done */
    if (ctx->cs_reg_error)
        g_simple_async_result_set_from_error (ctx->result, ctx->cs_reg_error);
    else
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    run_all_registration_checks_context_complete_and_free (ctx);
}

void
mm_iface_modem_3gpp_run_all_registration_checks (MMIfaceModem3gpp *self,
                                                 GAsyncReadyCallback callback,
                                                 gpointer user_data)
{
    RunAllRegistrationChecksContext *ctx;

    ctx = g_new0 (RunAllRegistrationChecksContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             mm_iface_modem_3gpp_run_all_registration_checks);

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, &ctx->ps_supported,
                  MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, &ctx->cs_supported,
                  NULL);

    mm_dbg ("Running registration checks (CS: '%s', PS: '%s')",
            ctx->cs_supported ? "yes" : "no",
            ctx->ps_supported ? "yes" : "no");

    if (ctx->cs_supported &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_cs_registration_check &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_cs_registration_check_finish) {
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_cs_registration_check (
            self,
            (GAsyncReadyCallback)run_cs_registration_check_ready,
            ctx);
        return;
    }

    if (ctx->ps_supported &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check &&
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check_finish) {
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->run_ps_registration_check (
            self,
            (GAsyncReadyCallback)run_ps_registration_check_ready,
            ctx);
        return;
    }

    /* Nothing to do :-/ all done */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    run_all_registration_checks_context_complete_and_free (ctx);
}

/*****************************************************************************/

#undef STR_REPLY_READY_FN
#define STR_REPLY_READY_FN(NAME, DISPLAY)                               \
    static void                                                         \
    load_##NAME##_ready (MMIfaceModem3gpp *self,                        \
                  GAsyncResult *res)                                    \
    {                                                                   \
        GError *error = NULL;                                           \
        MmGdbusModem3gpp *skeleton = NULL;                              \
        gchar *str;                                                     \
                                                                        \
        str = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_##NAME##_finish (self, res, &error); \
        if (error) {                                                    \
            mm_warn ("Couldn't load %s: '%s'", DISPLAY, error->message); \
            g_error_free (error);                                       \
            return;                                                     \
        }                                                               \
                                                                        \
        g_object_get (self,                                             \
                      MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,     \
                      NULL);                                            \
        mm_gdbus_modem3gpp_set_##NAME (skeleton, str);                  \
        g_free (str);                                                   \
        g_object_unref (skeleton);                                      \
    }

STR_REPLY_READY_FN (operator_code, "Operator Code")
STR_REPLY_READY_FN (operator_name, "Operator Name")

static void
set_bearer_3gpp_connection_allowed (MMBearer *bearer,
                                    const gboolean *roaming_network)
{
    /* Don't allow bearer to get connected if roaming forbidden */
    if (MM_IS_BEARER_3GPP (bearer)) {
        if (!*roaming_network ||
            mm_bearer_3gpp_get_allow_roaming (MM_BEARER_3GPP (bearer)))
            mm_bearer_set_connection_allowed (bearer);
        else
            mm_bearer_set_connection_forbidden (bearer,
                                                MM_BEARER_CONNECTION_FORBIDDEN_REASON_ROAMING);
    }
}

static void
bearer_3gpp_connection_allowed (MMIfaceModem3gpp *self,
                                gboolean roaming_network)
{
    MMBearerList *bearer_list = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &bearer_list,
                  NULL);
    if (!bearer_list)
        return;

    /* Once registered, allow 3GPP bearers to get connected */
    mm_bearer_list_foreach (bearer_list,
                            (MMBearerListForeachFunc)set_bearer_3gpp_connection_allowed,
                            &roaming_network);
    g_object_unref (bearer_list);
}

static void
set_bearer_3gpp_connection_forbidden (MMBearer *bearer)
{
    if (MM_IS_BEARER_3GPP (bearer))
        mm_bearer_set_connection_forbidden (
            bearer,
            MM_BEARER_CONNECTION_FORBIDDEN_REASON_UNREGISTERED);
}

static void
bearer_3gpp_connection_forbidden (MMIfaceModem3gpp *self)
{
    MMBearerList *bearer_list = NULL;

    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &bearer_list,
                  NULL);
    if (!bearer_list)
        return;

    /* Ensure all 3GPP bearers get disconnected and set connection forbidden */
    mm_bearer_list_foreach (bearer_list,
                            (MMBearerListForeachFunc)set_bearer_3gpp_connection_forbidden,
                            NULL);
    g_object_unref (bearer_list);
}

#define ALL_3GPP_ACCESS_TECHNOLOGIES_MASK         \
    (MM_MODEM_ACCESS_TECHNOLOGY_GSM |             \
     MM_MODEM_ACCESS_TECHNOLOGY_GSM_COMPACT |     \
     MM_MODEM_ACCESS_TECHNOLOGY_GPRS |            \
     MM_MODEM_ACCESS_TECHNOLOGY_EDGE |            \
     MM_MODEM_ACCESS_TECHNOLOGY_UMTS |            \
     MM_MODEM_ACCESS_TECHNOLOGY_HSDPA |           \
     MM_MODEM_ACCESS_TECHNOLOGY_HSUPA |           \
     MM_MODEM_ACCESS_TECHNOLOGY_HSPA |            \
     MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS |       \
     MM_MODEM_ACCESS_TECHNOLOGY_LTE)

static void
update_registration_state (MMIfaceModem3gpp *self,
                           MMModem3gppRegistrationState new_state,
                           MMModemAccessTechnology access_tech)
{
    MMModem3gppRegistrationState old_state;

    /* Only set new state if different */
    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, &old_state,
                  NULL);
    if (new_state != old_state) {
        GEnumClass *enum_class;
        GEnumValue *new_value;
        GEnumValue *old_value;
        const gchar *dbus_path;

        enum_class = G_ENUM_CLASS (g_type_class_ref (MM_TYPE_MODEM_3GPP_REGISTRATION_STATE));
        new_value = g_enum_get_value (enum_class, new_state);
        old_value = g_enum_get_value (enum_class, old_state);
        dbus_path = g_dbus_object_get_object_path (G_DBUS_OBJECT (self));
        mm_info ("Modem %s: 3GPP Registration state changed (%s -> %s)",
                 dbus_path,
                 old_value->value_nick,
                 new_value->value_nick);
        g_type_class_unref (enum_class);

        /* The property in the interface is bound to the property
         * in the skeleton, so just updating here is enough */
        g_object_set (self,
                      MM_IFACE_MODEM_3GPP_REGISTRATION_STATE, new_state,
                      NULL);

        switch (new_state) {
        case MM_MODEM_3GPP_REGISTRATION_STATE_HOME:
        case MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING:
            /* Allow connection in 3GPP bearers */
            bearer_3gpp_connection_allowed (
                self,
                new_state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING);

            /* Launch operator code update */
            if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_code &&
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_code_finish)
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_code (
                    self,
                    (GAsyncReadyCallback)load_operator_code_ready,
                    NULL);
            /* Launch operator name update */
            if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_name &&
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_name_finish)
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_operator_name (
                    self,
                    (GAsyncReadyCallback)load_operator_name_ready,
                    NULL);

            /* TODO: Update signal quality */

            mm_iface_modem_update_access_tech (MM_IFACE_MODEM (self),
                                               access_tech,
                                               ALL_3GPP_ACCESS_TECHNOLOGIES_MASK);

            mm_iface_modem_update_subsystem_state (MM_IFACE_MODEM (self),
                                                   SUBSYSTEM_3GPP,
                                                   MM_MODEM_STATE_REGISTERED,
                                                   MM_MODEM_STATE_REASON_NONE);
            break;

        case MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING:
        case MM_MODEM_3GPP_REGISTRATION_STATE_IDLE:
        case MM_MODEM_3GPP_REGISTRATION_STATE_DENIED:
        case MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN:
            mm_iface_modem_update_access_tech (MM_IFACE_MODEM (self),
                                               0,
                                               ALL_3GPP_ACCESS_TECHNOLOGIES_MASK);
            bearer_3gpp_connection_forbidden (self);
            mm_iface_modem_update_subsystem_state (
                MM_IFACE_MODEM (self),
                SUBSYSTEM_3GPP,
                (new_state == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING ?
                 MM_MODEM_STATE_SEARCHING :
                 MM_MODEM_STATE_ENABLED),
                MM_MODEM_STATE_REASON_NONE);
            break;
        }
    }
}

typedef struct {
    MMModem3gppRegistrationState cs;
    MMModem3gppRegistrationState ps;
} RegistrationStateContext;

static RegistrationStateContext *
get_registration_state_context (MMIfaceModem3gpp *self)
{
    RegistrationStateContext *ctx;

    if (G_UNLIKELY (!registration_state_context_quark))
        registration_state_context_quark =  (g_quark_from_static_string (
                                                 REGISTRATION_STATE_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), registration_state_context_quark);
    if (!ctx) {
        /* Create context and keep it as object data */
        ctx = g_new0 (RegistrationStateContext, 1);
        ctx->cs = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;
        ctx->ps = MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN;

        g_object_set_qdata_full (
            G_OBJECT (self),
            registration_state_context_quark,
            ctx,
            (GDestroyNotify)g_free);
    }

    return ctx;
}

static MMModem3gppRegistrationState
get_consolidated_reg_state (RegistrationStateContext *ctx)
{
    /* Some devices (Blackberries for example) will respond to +CGREG, but
     * return ERROR for +CREG, probably because their firmware is just stupid.
     * So here we prefer the +CREG response, but if we never got a successful
     * +CREG response, we'll take +CGREG instead.
     */
    if (ctx->cs == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        ctx->cs == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)
        return ctx->cs;

    if (ctx->ps == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
        ctx->ps == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING)
        return ctx->ps;

    if (ctx->cs == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING)
        return ctx->cs;

    if (ctx->ps == MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING)
        return ctx->ps;

    return ctx->cs;
}

void
mm_iface_modem_3gpp_update_cs_registration_state (MMIfaceModem3gpp *self,
                                                  MMModem3gppRegistrationState state,
                                                  MMModemAccessTechnology access_tech)
{
    RegistrationStateContext *ctx;
    gboolean supported = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, &supported,
                  NULL);

    if (!supported)
        return;

    ctx = get_registration_state_context (self);
    ctx->cs = state;
    update_registration_state (self, get_consolidated_reg_state (ctx), access_tech);
}

void
mm_iface_modem_3gpp_update_ps_registration_state (MMIfaceModem3gpp *self,
                                                  MMModem3gppRegistrationState state,
                                                  MMModemAccessTechnology access_tech)
{
    RegistrationStateContext *ctx;
    gboolean supported = FALSE;

    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, &supported,
                  NULL);

    if (!supported)
        return;

    ctx = get_registration_state_context (self);
    ctx->ps = state;
    update_registration_state (self, get_consolidated_reg_state (ctx), access_tech);
}

/*****************************************************************************/

typedef struct {
    guint timeout_source;
    gboolean running;
} RegistrationCheckContext;

static void
registration_check_context_free (RegistrationCheckContext *ctx)
{
    if (ctx->timeout_source)
        g_source_remove (ctx->timeout_source);
    g_free (ctx);
}

static void
periodic_registration_checks_ready (MMIfaceModem3gpp *self,
                                    GAsyncResult *res)
{
    RegistrationCheckContext *ctx;
    GError *error = NULL;

    mm_iface_modem_3gpp_run_all_registration_checks_finish (self, res, &error);
    if (error) {
        mm_dbg ("Couldn't refresh 3GPP registration status: '%s'", error->message);
        g_error_free (error);
    }

    /* Remove the running tag */
    ctx = g_object_get_qdata (G_OBJECT (self), registration_check_context_quark);
    ctx->running = FALSE;
}

static gboolean
periodic_registration_check (MMIfaceModem3gpp *self)
{
    RegistrationCheckContext *ctx;

    /* Only launch a new one if not one running already */
    ctx = g_object_get_qdata (G_OBJECT (self), registration_check_context_quark);
    if (!ctx->running) {
        ctx->running = TRUE;
        mm_iface_modem_3gpp_run_all_registration_checks (
            self,
            (GAsyncReadyCallback)periodic_registration_checks_ready,
            NULL);
    }
    return TRUE;
}

static void
periodic_registration_check_disable (MMIfaceModem3gpp *self)
{
    if (G_UNLIKELY (!registration_check_context_quark))
        registration_check_context_quark = (g_quark_from_static_string (
                                                REGISTRATION_CHECK_CONTEXT_TAG));

    /* Overwriting the data will free the previous context */
    g_object_set_qdata (G_OBJECT (self),
                        registration_check_context_quark,
                        NULL);

    mm_dbg ("Periodic 3GPP registration checks disabled");
}

static void
periodic_registration_check_enable (MMIfaceModem3gpp *self)
{
    RegistrationCheckContext *ctx;

    if (G_UNLIKELY (!registration_check_context_quark))
        registration_check_context_quark = (g_quark_from_static_string (
                                                REGISTRATION_CHECK_CONTEXT_TAG));

    ctx = g_object_get_qdata (G_OBJECT (self), registration_check_context_quark);

    /* If context is already there, we're already enabled */
    if (ctx)
        return;

    /* Create context and keep it as object data */
    mm_dbg ("Periodic 3GPP registration checks enabled");
    ctx = g_new0 (RegistrationCheckContext, 1);
    ctx->timeout_source = g_timeout_add_seconds (REGISTRATION_CHECK_TIMEOUT_SEC,
                                                 (GSourceFunc)periodic_registration_check,
                                                 self);
    g_object_set_qdata_full (G_OBJECT (self),
                             registration_check_context_quark,
                             ctx,
                             (GDestroyNotify)registration_check_context_free);
}

/*****************************************************************************/

typedef struct {
    GSimpleAsyncResult *result;
    GList *bearers;
    MMBearer3gpp *current;
} Disconnect3gppBearersContext;

static void
disconnect_3gpp_bearers_context_complete_and_free (Disconnect3gppBearersContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);

    if (ctx->current)
        g_object_unref (ctx->current);
    g_list_free_full (ctx->bearers, (GDestroyNotify) g_object_unref);
    g_object_unref (ctx->result);
    g_free (ctx);
}

static gboolean
disconnect_3gpp_bearers_finish (MMIfaceModem3gpp *self,
                                GAsyncResult *res,
                                GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void disconnect_next_bearer (Disconnect3gppBearersContext *ctx);

static void
disconnect_next_bearer_ready (MMBearer *bearer,
                              GAsyncResult *res,
                              Disconnect3gppBearersContext *ctx)
{
    GError *error = NULL;

    if (!mm_bearer_disconnect_finish (bearer, res, &error)) {
        g_simple_async_result_take_error (ctx->result, error);
        disconnect_3gpp_bearers_context_complete_and_free (ctx);
        return;
    }

    /* Go on with the next one */
    disconnect_next_bearer (ctx);
}

static void
disconnect_next_bearer (Disconnect3gppBearersContext *ctx)
{
    if (ctx->current)
        g_object_unref (ctx->current);

    if (!ctx->bearers) {
        /* All done */
        ctx->current = NULL;
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disconnect_3gpp_bearers_context_complete_and_free (ctx);
        return;
    }

    ctx->current = MM_BEARER_3GPP (ctx->bearers->data);
    ctx->bearers = g_list_delete_link (ctx->bearers, ctx->bearers);

    mm_bearer_disconnect (MM_BEARER (ctx->current),
                          (GAsyncReadyCallback)disconnect_next_bearer_ready,
                          ctx);
}

static void
find_3gpp_bearer (MMBearer *bearer,
                  GList **list)
{
    if (MM_IS_BEARER_3GPP (bearer))
        *list = g_list_prepend (*list, g_object_ref (bearer));
}

static void
disconnect_3gpp_bearers (MMIfaceModem3gpp *self,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
    Disconnect3gppBearersContext *ctx;
    MMBearerList *bearer_list = NULL;

    ctx = g_new0 (Disconnect3gppBearersContext, 1);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disconnect_3gpp_bearers);
    g_object_get (self,
                  MM_IFACE_MODEM_BEARER_LIST, &bearer_list,
                  NULL);

    if (bearer_list) {
        mm_bearer_list_foreach (bearer_list,
                                (MMBearerListForeachFunc)find_3gpp_bearer,
                                &(ctx->bearers));
        g_object_unref (bearer_list);
        disconnect_next_bearer (ctx);
        return;
    }

    /* No bearer list, we're done. */
    g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
    disconnect_3gpp_bearers_context_complete_and_free (ctx);
}

/*****************************************************************************/

typedef struct _DisablingContext DisablingContext;
static void interface_disabling_step (DisablingContext *ctx);

typedef enum {
    DISABLING_STEP_FIRST,
    DISABLING_STEP_DISCONNECT_BEARERS,
    DISABLING_STEP_PERIODIC_REGISTRATION_CHECKS,
    DISABLING_STEP_CLEANUP_PS_REGISTRATION,
    DISABLING_STEP_CLEANUP_CS_REGISTRATION,
    DISABLING_STEP_CLEANUP_UNSOLICITED_REGISTRATION,
    DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS,
    DISABLING_STEP_LAST
} DisablingStep;

struct _DisablingContext {
    MMIfaceModem3gpp *self;
    MMAtSerialPort *primary;
    DisablingStep step;
    GSimpleAsyncResult *result;
    MmGdbusModem *skeleton;
};

static DisablingContext *
disabling_context_new (MMIfaceModem3gpp *self,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    DisablingContext *ctx;

    ctx = g_new0 (DisablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->primary = g_object_ref (mm_base_modem_get_port_primary (MM_BASE_MODEM (self)));
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             disabling_context_new);
    ctx->step = DISABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);

    return ctx;
}

static void
disabling_context_complete_and_free (DisablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_3gpp_disable_finish (MMIfaceModem3gpp *self,
                                    GAsyncResult *res,
                                    GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
disconnect_3gpp_bearers_ready (MMIfaceModem3gpp *self,
                               GAsyncResult *res,
                               DisablingContext *ctx)
{
    GError *error = NULL;

    disconnect_3gpp_bearers_finish (self, res, &error);
    if (error) {
        mm_dbg ("Couldn't disconnect 3GPP bearers: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_disabling_step (ctx);
}

#undef VOID_REPLY_READY_FN
#define VOID_REPLY_READY_FN(NAME,DISPLAY)                               \
    static void                                                         \
    NAME##_ready (MMIfaceModem3gpp *self,                               \
                  GAsyncResult *res,                                    \
                  DisablingContext *ctx)                                \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->NAME##_finish (self, res, &error); \
        if (error) {                                                    \
            mm_dbg ("Couldn't %s: '%s'", DISPLAY, error->message);      \
            g_error_free (error);                                       \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_disabling_step (ctx);                                 \
    }

VOID_REPLY_READY_FN (cleanup_unsolicited_registration,
                     "cleanup unsolicited registration")
VOID_REPLY_READY_FN (cleanup_ps_registration,
                     "cleanup PS registration")
VOID_REPLY_READY_FN (cleanup_cs_registration,
                     "cleanup CS registration")
VOID_REPLY_READY_FN (disable_unsolicited_events,
                     "disable unsolicited events")

static void
interface_disabling_step (DisablingContext *ctx)
{
    switch (ctx->step) {
    case DISABLING_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_DISCONNECT_BEARERS:
        disconnect_3gpp_bearers (ctx->self,
                                 (GAsyncReadyCallback)disconnect_3gpp_bearers_ready,
                                 ctx);
        return;

    case DISABLING_STEP_PERIODIC_REGISTRATION_CHECKS:
        periodic_registration_check_disable (ctx->self);
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_CLEANUP_PS_REGISTRATION: {
        gboolean ps_supported = FALSE;

        g_object_get (ctx->self,
                      MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, &ps_supported,
                      NULL);

        if (ps_supported &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_ps_registration &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_ps_registration_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_ps_registration (
                ctx->self,
                (GAsyncReadyCallback)cleanup_ps_registration_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;
    }

    case DISABLING_STEP_CLEANUP_CS_REGISTRATION: {
        gboolean cs_supported = FALSE;

        g_object_get (ctx->self,
                      MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, &cs_supported,
                      NULL);

        if (cs_supported &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_cs_registration &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_cs_registration_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_cs_registration (
                ctx->self,
                (GAsyncReadyCallback)cleanup_cs_registration_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;
    }

    case DISABLING_STEP_CLEANUP_UNSOLICITED_REGISTRATION:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_unsolicited_registration &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_unsolicited_registration_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->cleanup_unsolicited_registration (
                ctx->self,
                (GAsyncReadyCallback)cleanup_unsolicited_registration_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_DISABLE_UNSOLICITED_EVENTS:
        if (G_UNLIKELY (!unsolicited_events_supported_quark))
            unsolicited_events_supported_quark = (g_quark_from_static_string (
                                                      UNSOLICITED_EVENTS_SUPPORTED_TAG));

        /* Only try to disable if supported */
        if (GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                  unsolicited_events_supported_quark))) {
            if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->disable_unsolicited_events &&
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->disable_unsolicited_events_finish) {
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->disable_unsolicited_events (
                    ctx->self,
                    (GAsyncReadyCallback)disable_unsolicited_events_ready,
                    ctx);
                return;
            }
        }
        /* Fall down to next step */
        ctx->step++;

    case DISABLING_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        disabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_3gpp_disable (MMIfaceModem3gpp *self,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
    interface_disabling_step (disabling_context_new (self,
                                                     callback,
                                                     user_data));
}

/*****************************************************************************/

typedef struct _EnablingContext EnablingContext;
static void interface_enabling_step (EnablingContext *ctx);

typedef enum {
    ENABLING_STEP_FIRST,
    ENABLING_STEP_SETUP_INDICATORS,
    ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS,
    ENABLING_STEP_SETUP_UNSOLICITED_REGISTRATION,
    ENABLING_STEP_SETUP_CS_REGISTRATION,
    ENABLING_STEP_SETUP_PS_REGISTRATION,
    ENABLING_STEP_RUN_ALL_REGISTRATION_CHECKS,
    ENABLING_STEP_LAST
} EnablingStep;

struct _EnablingContext {
    MMIfaceModem3gpp *self;
    MMAtSerialPort *primary;
    EnablingStep step;
    GSimpleAsyncResult *result;
    MmGdbusModem3gpp *skeleton;
};

static EnablingContext *
enabling_context_new (MMIfaceModem3gpp *self,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
    EnablingContext *ctx;

    ctx = g_new0 (EnablingContext, 1);
    ctx->self = g_object_ref (self);
    ctx->primary = g_object_ref (mm_base_modem_get_port_primary (MM_BASE_MODEM (self)));
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             enabling_context_new);
    ctx->step = ENABLING_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);

    return ctx;
}

static void
enabling_context_complete_and_free (EnablingContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->primary);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

gboolean
mm_iface_modem_3gpp_enable_finish (MMIfaceModem3gpp *self,
                                   GAsyncResult *res,
                                   GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

#undef VOID_REPLY_READY_FN
#define VOID_REPLY_READY_FN(NAME)                                       \
    static void                                                         \
    NAME##_ready (MMIfaceModem3gpp *self,                               \
                  GAsyncResult *res,                                    \
                  EnablingContext *ctx)                                 \
    {                                                                   \
        GError *error = NULL;                                           \
                                                                        \
        MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->NAME##_finish (self, res, &error); \
        if (error) {                                                    \
            g_simple_async_result_take_error (ctx->result, error);      \
            enabling_context_complete_and_free (ctx);                   \
            return;                                                     \
        }                                                               \
                                                                        \
        /* Go on to next step */                                        \
        ctx->step++;                                                    \
        interface_enabling_step (ctx);                                  \
    }

static void
setup_indicators_ready (MMIfaceModem3gpp *self,
                        GAsyncResult *res,
                        EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_indicators_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_dbg ("Indicator control setup failed: '%s'", error->message);
        g_error_free (error);

        /* If we get an error setting up indicators, don't even bother trying to
         * enable unsolicited events. */
        ctx->step = ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS + 1;
        interface_enabling_step (ctx);
        return;
    }

    /* Indicators setup, so assume we support unsolicited events */
    g_object_set_qdata (G_OBJECT (self),
                        unsolicited_events_supported_quark,
                        GUINT_TO_POINTER (TRUE));

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
enable_unsolicited_events_ready (MMIfaceModem3gpp *self,
                                 GAsyncResult *res,
                                 EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->enable_unsolicited_events_finish (self, res, &error);
    if (error) {
        /* This error shouldn't be treated as critical */
        mm_dbg ("Enabling unsolicited events failed: '%s'", error->message);
        g_error_free (error);

        /* Reset support flag */
        g_object_set_qdata (G_OBJECT (self),
                            unsolicited_events_supported_quark,
                            GUINT_TO_POINTER (FALSE));
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
setup_cs_registration_ready (MMIfaceModem3gpp *self,
                             GAsyncResult *res,
                             EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_cs_registration_finish (self, res, &error);
    if (error) {
        /* If error, setup periodic registration checks */
        periodic_registration_check_enable (ctx->self);
        mm_dbg ("Couldn't setup CS registration: '%s'",
                error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
setup_ps_registration_ready (MMIfaceModem3gpp *self,
                             GAsyncResult *res,
                             EnablingContext *ctx)
{
    GError *error = NULL;

    MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->setup_ps_registration_finish (self, res, &error);
    if (error) {
        /* If error, setup periodic registration checks */
        periodic_registration_check_enable (ctx->self);
        mm_dbg ("Couldn't setup PS registration: '%s'",
                error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

static void
run_all_registration_checks_ready (MMIfaceModem3gpp *self,
                                   GAsyncResult *res,
                                   EnablingContext *ctx)
{
    GError *error = NULL;

    mm_iface_modem_3gpp_run_all_registration_checks_finish (self, res, &error);
    if (error) {
        g_simple_async_result_take_error (ctx->result, error);
        enabling_context_complete_and_free (ctx);
        return;
    }

    /* Go on to next step */
    ctx->step++;
    interface_enabling_step (ctx);
}

VOID_REPLY_READY_FN (setup_unsolicited_registration)

static void
interface_enabling_step (EnablingContext *ctx)
{
    switch (ctx->step) {
    case ENABLING_STEP_FIRST:
        /* Setup quarks if we didn't do it before */
        if (G_UNLIKELY (!indicators_checked_quark))
            indicators_checked_quark = (g_quark_from_static_string (
                                            INDICATORS_CHECKED_TAG));
        if (G_UNLIKELY (!unsolicited_events_supported_quark))
            unsolicited_events_supported_quark = (g_quark_from_static_string (
                                                      UNSOLICITED_EVENTS_SUPPORTED_TAG));
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SETUP_INDICATORS:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                   indicators_checked_quark))) {
            /* Set the checked flag so that we don't run it again */
            g_object_set_qdata (G_OBJECT (ctx->self),
                                indicators_checked_quark,
                                GUINT_TO_POINTER (TRUE));
            /* Initially, assume we don't support unsolicited events */
            g_object_set_qdata (G_OBJECT (ctx->self),
                                unsolicited_events_supported_quark,
                                GUINT_TO_POINTER (FALSE));
            if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_indicators &&
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_indicators_finish) {
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_indicators (
                    ctx->self,
                    (GAsyncReadyCallback)setup_indicators_ready,
                    ctx);
                return;
            }
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_ENABLE_UNSOLICITED_EVENTS:
        if (!GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (ctx->self),
                                                   unsolicited_events_supported_quark))) {
            if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->enable_unsolicited_events &&
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->enable_unsolicited_events_finish) {
                MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->enable_unsolicited_events (
                    ctx->self,
                    (GAsyncReadyCallback)enable_unsolicited_events_ready,
                    ctx);
                return;
            }
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SETUP_UNSOLICITED_REGISTRATION:
        if (MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_registration &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_registration_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_unsolicited_registration (
                ctx->self,
                (GAsyncReadyCallback)setup_unsolicited_registration_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case ENABLING_STEP_SETUP_CS_REGISTRATION: {
        gboolean cs_supported = FALSE;

        g_object_get (ctx->self,
                      MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, &cs_supported,
                      NULL);

        if (cs_supported &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_cs_registration &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_cs_registration_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_cs_registration (
                ctx->self,
                (GAsyncReadyCallback)setup_cs_registration_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;
    }

    case ENABLING_STEP_SETUP_PS_REGISTRATION: {
        gboolean ps_supported = FALSE;

        g_object_get (ctx->self,
                      MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED, &ps_supported,
                      NULL);

        if (ps_supported &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_ps_registration &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_ps_registration_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->setup_ps_registration (
                ctx->self,
                (GAsyncReadyCallback)setup_ps_registration_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;
    }
    case ENABLING_STEP_RUN_ALL_REGISTRATION_CHECKS:
        mm_iface_modem_3gpp_run_all_registration_checks (
            ctx->self,
            (GAsyncReadyCallback)run_all_registration_checks_ready,
            ctx);
        return;

    case ENABLING_STEP_LAST:
        /* We are done without errors! */
        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        enabling_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

void
mm_iface_modem_3gpp_enable (MMIfaceModem3gpp *self,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    interface_enabling_step (enabling_context_new (self,
                                                   callback,
                                                   user_data));
}

/*****************************************************************************/

typedef struct _InitializationContext InitializationContext;
static void interface_initialization_step (InitializationContext *ctx);

typedef enum {
    INITIALIZATION_STEP_FIRST,
    INITIALIZATION_STEP_IMEI,
    INITIALIZATION_STEP_LAST
} InitializationStep;

struct _InitializationContext {
    MMIfaceModem3gpp *self;
    MMAtSerialPort *port;
    MmGdbusModem3gpp *skeleton;
    GSimpleAsyncResult *result;
    InitializationStep step;
};

static InitializationContext *
initialization_context_new (MMIfaceModem3gpp *self,
                            MMAtSerialPort *port,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
    InitializationContext *ctx;

    ctx = g_new0 (InitializationContext, 1);
    ctx->self = g_object_ref (self);
    ctx->port = g_object_ref (port);
    ctx->result = g_simple_async_result_new (G_OBJECT (self),
                                             callback,
                                             user_data,
                                             initialization_context_new);
    ctx->step = INITIALIZATION_STEP_FIRST;
    g_object_get (ctx->self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &ctx->skeleton,
                  NULL);
    g_assert (ctx->skeleton != NULL);
    return ctx;
}

static void
initialization_context_complete_and_free (InitializationContext *ctx)
{
    g_simple_async_result_complete_in_idle (ctx->result);
    g_object_unref (ctx->self);
    g_object_unref (ctx->port);
    g_object_unref (ctx->result);
    g_object_unref (ctx->skeleton);
    g_free (ctx);
}

static void
load_imei_ready (MMIfaceModem3gpp *self,
                 GAsyncResult *res,
                 InitializationContext *ctx)
{
    GError *error = NULL;
    gchar *imei;

    imei = MM_IFACE_MODEM_3GPP_GET_INTERFACE (self)->load_imei_finish (self, res, &error);
    mm_gdbus_modem3gpp_set_imei (ctx->skeleton, imei);
    g_free (imei);

    if (error) {
        mm_warn ("couldn't load IMEI: '%s'", error->message);
        g_error_free (error);
    }

    /* Go on to next step */
    ctx->step++;
    interface_initialization_step (ctx);
}

static void
interface_initialization_step (InitializationContext *ctx)
{
    switch (ctx->step) {
    case INITIALIZATION_STEP_FIRST:
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_IMEI:
        /* IMEI value is meant to be loaded only once during the whole
         * lifetime of the modem. Therefore, if we already have it loaded,
         * don't try to load it again. */
        if (!mm_gdbus_modem3gpp_get_imei (ctx->skeleton) &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_imei &&
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_imei_finish) {
            MM_IFACE_MODEM_3GPP_GET_INTERFACE (ctx->self)->load_imei (
                ctx->self,
                (GAsyncReadyCallback)load_imei_ready,
                ctx);
            return;
        }
        /* Fall down to next step */
        ctx->step++;

    case INITIALIZATION_STEP_LAST:
        /* We are done without errors! */

        /* Handle method invocations */
        g_signal_connect (ctx->skeleton,
                          "handle-register",
                          G_CALLBACK (handle_register),
                          ctx->self);
        g_signal_connect (ctx->skeleton,
                          "handle-scan",
                          G_CALLBACK (handle_scan),
                          ctx->self);


        /* Finally, export the new interface */
        mm_gdbus_object_skeleton_set_modem3gpp (MM_GDBUS_OBJECT_SKELETON (ctx->self),
                                                MM_GDBUS_MODEM3GPP (ctx->skeleton));

        g_simple_async_result_set_op_res_gboolean (ctx->result, TRUE);
        initialization_context_complete_and_free (ctx);
        return;
    }

    g_assert_not_reached ();
}

gboolean
mm_iface_modem_3gpp_initialize_finish (MMIfaceModem3gpp *self,
                                       GAsyncResult *res,
                                       GError **error)
{
    g_return_val_if_fail (MM_IS_IFACE_MODEM_3GPP (self), FALSE);
    g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);

    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

void
mm_iface_modem_3gpp_initialize (MMIfaceModem3gpp *self,
                                MMAtSerialPort *port,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    MmGdbusModem3gpp *skeleton = NULL;

    g_return_if_fail (MM_IS_IFACE_MODEM_3GPP (self));

    /* Did we already create it? */
    g_object_get (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, &skeleton,
                  NULL);
    if (!skeleton) {
        skeleton = mm_gdbus_modem3gpp_skeleton_new ();

        /* Set all initial property defaults */
        mm_gdbus_modem3gpp_set_imei (skeleton, NULL);
        mm_gdbus_modem3gpp_set_operator_code (skeleton, NULL);
        mm_gdbus_modem3gpp_set_operator_name (skeleton, NULL);
        mm_gdbus_modem3gpp_set_enabled_facility_locks (skeleton, MM_MODEM_3GPP_FACILITY_NONE);

        /* Bind our RegistrationState property */
        g_object_bind_property (self, MM_IFACE_MODEM_3GPP_REGISTRATION_STATE,
                                skeleton, "registration-state",
                                G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

        g_object_set (self,
                      MM_IFACE_MODEM_3GPP_DBUS_SKELETON, skeleton,
                      NULL);

        /* If the modem is *only* LTE, we assume that CS network is not
         * supported */
        if (mm_iface_modem_is_3gpp_lte_only (MM_IFACE_MODEM (self))) {
            mm_dbg ("Modem is LTE-only, assuming CS network is not supported");
            g_object_set (self,
                          MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED, FALSE,
                          NULL);
        }
    }

    /* Perform async initialization here */
    interface_initialization_step (initialization_context_new (self,
                                                               port,
                                                               callback,
                                                               user_data));
    g_object_unref (skeleton);
    return;
}

void
mm_iface_modem_3gpp_shutdown (MMIfaceModem3gpp *self)
{
    g_return_if_fail (MM_IS_IFACE_MODEM_3GPP (self));

    /* Unexport DBus interface and remove the skeleton */
    mm_gdbus_object_skeleton_set_modem3gpp (MM_GDBUS_OBJECT_SKELETON (self), NULL);
    g_object_set (self,
                  MM_IFACE_MODEM_3GPP_DBUS_SKELETON, NULL,
                  NULL);
}

/*****************************************************************************/

static void
iface_modem_3gpp_init (gpointer g_iface)
{
    static gboolean initialized = FALSE;

    if (initialized)
        return;

    /* Properties */
    g_object_interface_install_property
        (g_iface,
         g_param_spec_object (MM_IFACE_MODEM_3GPP_DBUS_SKELETON,
                              "3GPP DBus skeleton",
                              "DBus skeleton for the 3GPP interface",
                              MM_GDBUS_TYPE_MODEM3GPP_SKELETON,
                              G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_enum (MM_IFACE_MODEM_3GPP_REGISTRATION_STATE,
                            "RegistrationState",
                            "Registration state of the modem",
                            MM_TYPE_MODEM_3GPP_REGISTRATION_STATE,
                            MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN,
                            G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_IFACE_MODEM_3GPP_CS_NETWORK_SUPPORTED,
                               "CS network supported",
                               "Whether the modem works in the CS network",
                               TRUE,
                               G_PARAM_READWRITE));

    g_object_interface_install_property
        (g_iface,
         g_param_spec_boolean (MM_IFACE_MODEM_3GPP_PS_NETWORK_SUPPORTED,
                               "PS network supported",
                               "Whether the modem works in the PS network",
                               TRUE,
                               G_PARAM_READWRITE));

    initialized = TRUE;
}

GType
mm_iface_modem_3gpp_get_type (void)
{
    static GType iface_modem_3gpp_type = 0;

    if (!G_UNLIKELY (iface_modem_3gpp_type)) {
        static const GTypeInfo info = {
            sizeof (MMIfaceModem3gpp), /* class_size */
            iface_modem_3gpp_init,      /* base_init */
            NULL,                  /* base_finalize */
        };

        iface_modem_3gpp_type = g_type_register_static (G_TYPE_INTERFACE,
                                                        "MMIfaceModem3gpp",
                                                        &info,
                                                        0);

        g_type_interface_add_prerequisite (iface_modem_3gpp_type, MM_TYPE_IFACE_MODEM);
    }

    return iface_modem_3gpp_type;
}
