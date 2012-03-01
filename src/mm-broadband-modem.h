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
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef MM_BROADBAND_MODEM_H
#define MM_BROADBAND_MODEM_H

#include <glib.h>
#include <glib-object.h>

#include "mm-base-modem.h"

#define MM_TYPE_BROADBAND_MODEM            (mm_broadband_modem_get_type ())
#define MM_BROADBAND_MODEM(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM, MMBroadbandModem))
#define MM_BROADBAND_MODEM_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM, MMBroadbandModemClass))
#define MM_IS_BROADBAND_MODEM(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM))
#define MM_IS_BROADBAND_MODEM_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM))
#define MM_BROADBAND_MODEM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM, MMBroadbandModemClass))

typedef struct _MMBroadbandModem MMBroadbandModem;
typedef struct _MMBroadbandModemClass MMBroadbandModemClass;
typedef struct _MMBroadbandModemPrivate MMBroadbandModemPrivate;

struct _MMBroadbandModem {
    MMBaseModem parent;
    MMBroadbandModemPrivate *priv;
};

struct _MMBroadbandModemClass {
    MMBaseModemClass parent;
};

GType mm_broadband_modem_get_type (void);

MMBroadbandModem *mm_broadband_modem_new (const gchar *device,
                                          const gchar *driver,
                                          const gchar *plugin,
                                          guint16 vendor_id,
                                          guint16 product_id);

#endif /* MM_BROADBAND_MODEM_H */

