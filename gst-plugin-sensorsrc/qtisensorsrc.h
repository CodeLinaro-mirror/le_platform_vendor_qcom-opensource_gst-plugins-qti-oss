/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __GST_QTI_SENSOR_SRC_H__
#define __GST_QTI_SENSOR_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <glib.h>
#include <dlfcn.h>

#include <sensor-lib/lib-sensor-c-api.h>

G_BEGIN_DECLS

#define GST_TYPE_SENSOR_SRC (gst_sensor_src_get_type ())

G_DECLARE_FINAL_TYPE (GstSensorSrc, gst_sensor_src, GST, SENSORSRC, GstPushSrc)

struct _GstSensorSrc {
  GstPushSrc   parent;
  GstClockTime tsbase;

  gchar        *sensor_name;
  gint         sample_rate;

  void         *swl_handle;
  void         *sensor_handle;

  create_sensor_fn       create_sensor;
  get_sensor_data_fn     get_sensor_data;
  destroy_sensor_data_fn destroy_sensor_data;
  stop_stream_fn         stop_stream;
  destroy_sensor_fn      destroy_sensor;
};

struct _GstSensorSrcClass {
  GstPushSrcClass parent_class;
};

#endif // __GST_QTI_SENSOR_SRC_H__
