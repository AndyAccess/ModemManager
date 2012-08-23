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
 * Copyright (C) 2012 Google Inc.
 */

#ifndef MM_BROADBAND_MODEM_QMI_H
#define MM_BROADBAND_MODEM_QMI_H

#include "mm-broadband-modem.h"

#define MM_TYPE_BROADBAND_MODEM_QMI            (mm_broadband_modem_qmi_get_type ())
#define MM_BROADBAND_MODEM_QMI(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_BROADBAND_MODEM_QMI, MMBroadbandModemQmi))
#define MM_BROADBAND_MODEM_QMI_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_BROADBAND_MODEM_QMI, MMBroadbandModemQmiClass))
#define MM_IS_BROADBAND_MODEM_QMI(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_BROADBAND_MODEM_QMI))
#define MM_IS_BROADBAND_MODEM_QMI_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_BROADBAND_MODEM_QMI))
#define MM_BROADBAND_MODEM_QMI_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_BROADBAND_MODEM_QMI, MMBroadbandModemQmiClass))

typedef struct _MMBroadbandModemQmi MMBroadbandModemQmi;
typedef struct _MMBroadbandModemQmiClass MMBroadbandModemQmiClass;

struct _MMBroadbandModemQmi {
    MMBroadbandModem parent;
};

struct _MMBroadbandModemQmiClass{
    MMBroadbandModemClass parent;
};

GType mm_broadband_modem_qmi_get_type (void);

MMBroadbandModemQmi *mm_broadband_modem_qmi_new (const gchar *device,
                                                 const gchar *driver,
                                                 const gchar *plugin,
                                                 guint16 vendor_id,
                                                 guint16 product_id);

#endif /* MM_BROADBAND_MODEM_QMI_H */
