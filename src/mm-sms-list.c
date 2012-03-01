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
#include <ctype.h>

#include <ModemManager.h>
#include <libmm-common.h>

#include "mm-iface-modem-messaging.h"
#include "mm-marshal.h"
#include "mm-sms-list.h"
#include "mm-sms.h"
#include "mm-utils.h"
#include "mm-log.h"

G_DEFINE_TYPE (MMSmsList, mm_sms_list, G_TYPE_OBJECT);

enum {
    PROP_0,
    PROP_MODEM,
    PROP_LAST
};
static GParamSpec *properties[PROP_LAST];

enum {
    SIGNAL_ADDED,
    SIGNAL_DELETED,
    SIGNAL_LAST
};
static guint signals[SIGNAL_LAST];

struct _MMSmsListPrivate {
    /* The owner modem */
    MMBaseModem *modem;
    /* List of sms objects */
    GList *list;
};

/*****************************************************************************/

guint
mm_sms_list_get_count (MMSmsList *self)
{
    return g_list_length (self->priv->list);
}

GStrv
mm_sms_list_get_paths (MMSmsList *self)
{
    GStrv path_list = NULL;
    GList *l;
    guint i;

    path_list = g_new0 (gchar *,
                        1 + g_list_length (self->priv->list));

    for (i = 0, l = self->priv->list; l; l = g_list_next (l))
        path_list[i++] = g_strdup (mm_sms_get_path (MM_SMS (l->data)));

    return path_list;
}

/*****************************************************************************/

gboolean
mm_sms_list_delete_sms_finish (MMSmsList *self,
                               GAsyncResult *res,
                               GError **error)
{
    return !g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res), error);
}

static void
delete_ready (MMSms *sms,
              GAsyncResult *res,
              GSimpleAsyncResult *simple)
{
    GError *error = NULL;

    if (!mm_sms_delete_finish (sms, res, &error))
        /* We report the error, but we really get the SMS removed */
        g_simple_async_result_take_error (simple, error);
    else
        g_simple_async_result_set_op_res_gboolean (simple, TRUE);

    /* We don't need to unref the SMS any more, but we can use the
     * reference we got in the method, which is the one kept alive
     * during the async operation. */
    g_signal_emit (MM_SMS_LIST (g_async_result_get_source_object (G_ASYNC_RESULT (simple))),
                   signals[SIGNAL_DELETED], 0,
                   mm_sms_get_path (sms));

    g_simple_async_result_complete (simple);
    g_object_unref (simple);
}

static guint
cmp_sms_by_path (MMSms *sms,
                 const gchar *path)
{
    return strcmp (mm_sms_get_path (sms), path);
}

void
mm_sms_list_delete_sms (MMSmsList *self,
                        const gchar *sms_path,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
    GSimpleAsyncResult *result;
    MMSms *sms;
    GList *l;

    result = g_simple_async_result_new (G_OBJECT (self),
                                        callback,
                                        user_data,
                                        mm_sms_list_delete_sms);

    l = g_list_find_custom (self->priv->list,
                            (gpointer)sms_path,
                            (GCompareFunc)cmp_sms_by_path);
    if (!l) {
        g_simple_async_result_set_error (result,
                                         MM_CORE_ERROR,
                                         MM_CORE_ERROR_NOT_FOUND,
                                         "No SMS found with path '%s'",
                                         sms_path);
        g_simple_async_result_complete_in_idle (result);
        g_object_unref (result);
        return;
    }

    /* Remove it from our list of SMS objects */
    sms = l->data;
    self->priv->list = g_list_delete_link (self->priv->list, l);

    mm_sms_delete (sms,
                   (GAsyncReadyCallback)delete_ready,
                   result);

    /* We do remove here our last reference to the SMS object. The
     * async method will keep its own while the operation is ongoing. */
    g_object_unref (sms);
}

/*****************************************************************************/

static guint
cmp_sms_by_concat_reference (MMSms *sms,
                             gpointer user_data)
{
    if (!mm_sms_is_multipart (sms))
        return -1;

    return (GPOINTER_TO_UINT (user_data) - mm_sms_get_multipart_reference (sms));
}

static guint
cmp_sms_by_part_index (MMSms *sms,
                       gpointer user_data)
{
    return !mm_sms_has_part_index (sms, GPOINTER_TO_UINT (user_data));
}

static void
take_singlepart (MMSmsList *self,
                 MMSmsPart *part,
                 gboolean received)
{
    MMSms *sms;
    GError *error = NULL;

    sms = mm_sms_singlepart_new (self->priv->modem,
                                 (received ?
                                  MM_MODEM_SMS_STATE_RECEIVED :
                                  MM_MODEM_SMS_STATE_STORED),
                                 part,
                                 &error);

    if (!sms) {
        mm_warn ("Couldn't create single-part SMS: '%s'", error->message);
        g_error_free (error);
    } else {
        self->priv->list = g_list_prepend (self->priv->list, sms);
        g_signal_emit (self, signals[SIGNAL_ADDED], 0,
                       mm_sms_get_path (sms),
                       received);
    }
}

static gboolean
take_multipart (MMSmsList *self,
                MMSmsPart *part,
                gboolean received,
                GError **error)
{
    GList *l;
    MMSms *sms;
    guint concat_reference;

    concat_reference = mm_sms_part_get_concat_reference (part);
    l = g_list_find_custom (self->priv->list,
                            GUINT_TO_POINTER (concat_reference),
                            (GCompareFunc)cmp_sms_by_concat_reference);
    if (l)  {
        sms = MM_SMS (l->data);
        /* Try to take the part */
        if (!mm_sms_multipart_take_part (sms, part, error))
            return FALSE;
    } else {
        /* Create new Multipart */
        sms = mm_sms_multipart_new (self->priv->modem,
                                    (received ?
                                     MM_MODEM_SMS_STATE_RECEIVED :
                                     MM_MODEM_SMS_STATE_STORED),
                                    concat_reference,
                                    mm_sms_part_get_concat_max (part),
                                    part,
                                    error);
        if (!sms)
            return FALSE;

        self->priv->list = g_list_prepend (self->priv->list, sms);
    }

    /* Check if completed and assembled */
    if (mm_sms_multipart_is_complete (sms) &&
        mm_sms_multipart_is_assembled (sms))
        g_signal_emit (self, signals[SIGNAL_ADDED], 0,
                       mm_sms_get_path (sms));

    return TRUE;
}

gboolean
mm_sms_list_take_part (MMSmsList *self,
                       MMSmsPart *part,
                       gboolean received,
                       GError **error)
{
    /* Ensure we don't have already taken a part with the same index */
    if (g_list_find_custom (self->priv->list,
                            GUINT_TO_POINTER (mm_sms_part_get_index (part)),
                            (GCompareFunc)cmp_sms_by_part_index)) {
        g_set_error (error,
                     MM_CORE_ERROR,
                     MM_CORE_ERROR_FAILED,
                     "A part with index %u was already taken",
                     mm_sms_part_get_index (part));
        return FALSE;
    }

    /* Did we just get a part of a multi-part SMS? */
    if (mm_sms_part_should_concat (part))
        return take_multipart (self, part, received, error);

    /* Otherwise, we build a whole new single-part MMSms just from this part */
    take_singlepart (self, part, received);
    return TRUE;
}

/*****************************************************************************/

MMSmsList *
mm_sms_list_new (MMBaseModem *modem)
{
    /* Create the object */
    return g_object_new  (MM_TYPE_SMS_LIST,
                          MM_SMS_LIST_MODEM, modem,
                          NULL);
}

static void
set_property (GObject *object,
              guint prop_id,
              const GValue *value,
              GParamSpec *pspec)
{
    MMSmsList *self = MM_SMS_LIST (object);

    switch (prop_id) {
    case PROP_MODEM:
        g_clear_object (&self->priv->modem);
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
    MMSmsList *self = MM_SMS_LIST (object);

    switch (prop_id) {
    case PROP_MODEM:
        g_value_set_object (value, self->priv->modem);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
mm_sms_list_init (MMSmsList *self)
{
    /* Initialize private data */
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
                                              MM_TYPE_SMS_LIST,
                                              MMSmsListPrivate);
}

static void
dispose (GObject *object)
{
    MMSmsList *self = MM_SMS_LIST (object);

    g_clear_object (&self->priv->modem);
    g_list_free_full (self->priv->list, (GDestroyNotify)g_object_unref);

    G_OBJECT_CLASS (mm_sms_list_parent_class)->dispose (object);
}

static void
mm_sms_list_class_init (MMSmsListClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMSmsListPrivate));

    /* Virtual methods */
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->dispose = dispose;

    /* Properties */
    properties[PROP_MODEM] =
        g_param_spec_object (MM_SMS_LIST_MODEM,
                             "Modem",
                             "The Modem which owns this SMS list",
                             MM_TYPE_BASE_MODEM,
                             G_PARAM_READWRITE);
    g_object_class_install_property (object_class, PROP_MODEM, properties[PROP_MODEM]);

    /* Signals */
    signals[SIGNAL_ADDED] =
        g_signal_new (MM_SMS_ADDED,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMSmsListClass, sms_added),
                      NULL, NULL,
                      mm_marshal_VOID__STRING_BOOLEAN,
                      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_BOOLEAN);

    signals[SIGNAL_DELETED] =
        g_signal_new (MM_SMS_DELETED,
                      G_OBJECT_CLASS_TYPE (object_class),
                      G_SIGNAL_RUN_FIRST,
                      G_STRUCT_OFFSET (MMSmsListClass, sms_deleted),
                      NULL, NULL,
                      mm_marshal_VOID__STRING,
                      G_TYPE_NONE, 1, G_TYPE_STRING);
}
