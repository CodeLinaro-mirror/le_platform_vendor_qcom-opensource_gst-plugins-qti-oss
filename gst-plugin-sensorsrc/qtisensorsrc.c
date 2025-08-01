/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "qtisensorsrc.h"

#define DEFAULT_PROP_SAMPLE_RATE 10
#define DEFAULT_PROP_SENSOR_NAME "accel"

GST_DEBUG_CATEGORY_STATIC (gst_sensor_src_debug);
#define GST_CAT_DEFAULT gst_sensor_src_debug

static gboolean
load_symbol (gpointer *method, gpointer handle, const gchar *name)
{
  *method = dlsym (handle, name);

  if (*method == NULL) {
    GST_ERROR ("Failed to link library method %s, error: %s", name, dlerror ());
    return FALSE;
  }

  return TRUE;
}

G_DEFINE_TYPE (GstSensorSrc, gst_sensor_src, GST_TYPE_PUSH_SRC)

#define GST_SENSOR_SRC_CAPS \
  "sensor/x-msg"

static
GstStaticPadTemplate sensor_src_template =
  GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_SENSOR_SRC_CAPS));

enum
{
  PROP_0,
  PROP_SENSOR_NAME,
  PROP_SAMPLE_RATE,
};

static void
gst_sensor_src_dispose (GObject *object)
{
  GstSensorSrc *sensor_src = GST_SENSORSRC (object);

  // Destroy the sensor object
  if (sensor_src->destroy_sensor && sensor_src->sensor_handle) {
    sensor_src->destroy_sensor (sensor_src->sensor_handle);
    sensor_src->sensor_handle = NULL;
  }

  G_OBJECT_CLASS (gst_sensor_src_parent_class)->dispose (object);
}

static void
gst_sensor_src_finalize (GObject * object)
{
  GstSensorSrc *sensor_src = GST_SENSORSRC (object);

  // Free the sensor name string
  g_free (sensor_src->sensor_name);
  sensor_src->sensor_name = NULL;

  // Unload the shared library
  if (sensor_src->swl_handle) {
    dlclose (sensor_src->swl_handle);
    sensor_src->swl_handle = NULL;
  }

  G_OBJECT_CLASS (gst_sensor_src_parent_class)->finalize (object);
}

static void
gst_sensor_src_get_property (GObject *object, guint prop_id, GValue *value,
    GParamSpec *pspec)
{
  GstSensorSrc *sensor_src = GST_SENSORSRC (object);

  GST_OBJECT_LOCK (sensor_src);
  // Retrieve object properties here
  switch (prop_id) {
    case PROP_SENSOR_NAME:
      g_value_set_string (value, sensor_src->sensor_name);
      break;
    case PROP_SAMPLE_RATE:
      g_value_set_int (value, sensor_src->sample_rate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (sensor_src);
}

static void
gst_sensor_src_set_property (GObject *object, guint prop_id, const GValue *value,
    GParamSpec *pspec)
{
  GstSensorSrc *sensor_src = GST_SENSORSRC (object);

  GST_OBJECT_LOCK (sensor_src);
  switch (prop_id) {
    case PROP_SENSOR_NAME:
      sensor_src->sensor_name = g_strdup (g_value_get_string(value));
      GST_INFO_OBJECT (sensor_src, "Set sensor_name: %s",
          sensor_src->sensor_name);
      break;
    case PROP_SAMPLE_RATE:
      sensor_src->sample_rate = g_value_get_int (value);
      GST_INFO_OBJECT (sensor_src, "Set sample_rate: %d",
          sensor_src->sample_rate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (sensor_src);
}

static GstCaps* gst_sensorsrc_get_caps (GstBaseSrc *src, GstCaps *filter) {
  GstSensorSrc *self = GST_SENSORSRC (src);

  GstCaps *caps = gst_caps_new_simple ("sensor/x-msg",
      "sample-rate", G_TYPE_INT, self->sample_rate, NULL);

  if (filter) {
    GstCaps *intersection = gst_caps_intersect_full(filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref(caps);
    return intersection;
  }

  return caps;
}

static gboolean
gst_sensor_src_start (GstBaseSrc *src)
{
  GstSensorSrc *sensor_src = GST_SENSORSRC (src);
  GstPad *srcpad = GST_BASE_SRC_PAD (src);

  // Send stream-start event
  gchar *stream_id = gst_pad_create_stream_id (srcpad, GST_ELEMENT (src), NULL);
  GstEvent *stream_start = gst_event_new_stream_start (stream_id);
  g_free (stream_id);
  if (!gst_pad_push_event (srcpad, stream_start)) {
    GST_ERROR_OBJECT (sensor_src, "Failed to push stream-start event");
    return FALSE;
  }

  // Set dynamic caps
  GstCaps *caps = gst_caps_new_simple ("sensor/x-msg",
      "sample-rate", G_TYPE_INT, sensor_src->sample_rate, NULL);

  if (!gst_pad_set_caps (srcpad, caps)) {
    GST_ERROR_OBJECT (sensor_src, "Failed to set caps");
    gst_caps_unref (caps);
    return FALSE;
  }
  gst_caps_unref (caps);

  // Start Segment
  GstSegment segment;
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 0;
  segment.time = 0;
  if (!gst_pad_push_event (srcpad, gst_event_new_segment (&segment))) {
    GST_ERROR_OBJECT (sensor_src, "Failed to push segment event");
    return FALSE;
  }

  sensor_src->tsbase = GST_CLOCK_TIME_NONE;

  // Create the sensor using the dynamically loaded function
  if (NULL != sensor_src->sensor_name && 0 != sensor_src->sample_rate) {
    sensor_src->sensor_handle = sensor_src->create_sensor (
        sensor_src->sensor_name,
        sensor_src->sample_rate, 1);
  } else {
    GST_ERROR_OBJECT (sensor_src, "Invalid parameters!");
    return FALSE;
  }

  if (!sensor_src->sensor_handle) {
    GST_ERROR_OBJECT (sensor_src, "Failed to create sensor instance using C API");
    return FALSE;
  }

  GST_INFO_OBJECT (sensor_src, "Sensor streaming started via C API");
  return TRUE;
}

static gboolean
gst_sensor_src_stop (GstBaseSrc * bsrc)
{
  GstSensorSrc *sensor_src = GST_SENSORSRC (bsrc);

  // Stop the sensor stream
  if (sensor_src->stop_stream && sensor_src->sensor_handle) {
    sensor_src->stop_stream (sensor_src->sensor_handle);
  } else {
    GST_ERROR_OBJECT (sensor_src, "Failed to stop sensor!");
    return FALSE;
  }

  return TRUE;
}

static GstClockTime
running_time (GstPushSrc *src)
{
  GstElement *element = GST_ELEMENT (src);
  GstClock *clock = gst_element_get_clock (element);
  GstClockTime runningtime = GST_CLOCK_TIME_NONE;

  runningtime =
      gst_clock_get_time (clock) - gst_element_get_base_time (element);

  gst_object_unref (clock);

  return runningtime;
}

static GstFlowReturn
gst_sensor_src_fill (GstPushSrc *src, GstBuffer *buf)
{
  GstSensorSrc *sensor_src = GST_SENSORSRC (src);

  if (!sensor_src->get_sensor_data || !sensor_src->destroy_sensor_data) {
    GST_ERROR_OBJECT (src, "Sensor API function pointers are not set");
    return GST_FLOW_ERROR;
  }

  CSensorData* data = sensor_src->get_sensor_data (sensor_src->sensor_handle);
  if (!data || data->data_size <= 0) {
    GST_DEBUG_OBJECT (src, "Received empty or null CSensorData");
    gst_buffer_set_size (buf, 0);
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_GAP);
    return GST_FLOW_OK;
  }

  // Initialize timestamp
  sensor_src->tsbase = (GST_CLOCK_TIME_NONE == sensor_src->tsbase) ?
      data->timestamp - running_time (GST_PUSH_SRC (sensor_src)) :
      sensor_src->tsbase;

  GST_BUFFER_PTS (buf) = data->timestamp - sensor_src->tsbase;
  GST_BUFFER_DTS (buf) = GST_CLOCK_TIME_NONE;

  gint byte_size = data->data_size * sizeof (float);
  gst_buffer_set_size (buf, byte_size);

  GstMapInfo map;
  if (!gst_buffer_map (buf, &map, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (src, "Failed to map GstBuffer");
    sensor_src->destroy_sensor_data (data);
    return GST_FLOW_ERROR;
  }

  memcpy (map.data, data->batch_data, byte_size);
  gst_buffer_unmap (buf, &map);

  GstStructure* structure = gst_structure_new (
      "SENSOR_DATA",
      "sensor", G_TYPE_STRING, data->sensor_type,
      "dataSize", G_TYPE_INT, data->data_size,
      "timestamp", G_TYPE_UINT, data->timestamp,
      NULL
  );

  if (!structure) {
    GST_ERROR_OBJECT (src, "Failed to create metadata structure");
    sensor_src->destroy_sensor_data (data);
    return GST_FLOW_ERROR;
  }

  gst_mini_object_set_qdata(
      GST_MINI_OBJECT (buf),
      g_quark_from_string ("sensor_data"),
      structure,
      (GDestroyNotify) gst_structure_free
  );

  sensor_src->destroy_sensor_data (data);
  return GST_FLOW_OK;
}

static void
gst_sensor_src_init (GstSensorSrc * src)
{
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);

  src->sensor_name = NULL;
  src->sensor_handle = NULL;
  src->sample_rate = 0;
  src->swl_handle = NULL;

  GST_DEBUG_CATEGORY_INIT (gst_sensor_src_debug, "qtisensorsrc", 0,
      "qtisensorsrc object");

    // Load the shared library
  src->swl_handle = dlopen ("libsensor-wrapper.so", RTLD_LAZY);
  if (!src->swl_handle) {
    GST_ERROR_OBJECT(src, "Failed to load libsensor-wrapper.so: %s", dlerror ());
    return;
  }

  // Load the required symbols from the shared library
  gboolean success = TRUE;

  success &= load_symbol ((gpointer*)&src->create_sensor,
      src->swl_handle, "create_sensor");
  success &= load_symbol ((gpointer*)&src->get_sensor_data,
      src->swl_handle, "get_sensor_data");
  success &= load_symbol ((gpointer*)&src->destroy_sensor_data,
      src->swl_handle, "destroy_sensor_data");
  success &= load_symbol ((gpointer*)&src->stop_stream,
      src->swl_handle, "stop_stream");
  success &= load_symbol ((gpointer*)&src->destroy_sensor, src->swl_handle,
      "destroy_sensor");

  if (!success) {
    GST_ERROR_OBJECT (src, "One or more required symbols failed to load.");
    dlclose (src->swl_handle);
    src->swl_handle = NULL;
    return;
  }
}

static void
gst_sensor_src_class_init (GstSensorSrcClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *gstbasesrc = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *gstpushsrc = GST_PUSH_SRC_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_sensor_src_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_sensor_src_get_property);
  gobject->dispose      = GST_DEBUG_FUNCPTR (gst_sensor_src_dispose);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_sensor_src_finalize);

  gst_element_class_set_static_metadata (gstelement,
      "QTI Sensor Source Element", "Sensor Source Element",
      "This plugin receive GST buffer over Sensor", "QTI");

  gst_element_class_add_static_pad_template (gstelement, &sensor_src_template);

  gstbasesrc->start = GST_DEBUG_FUNCPTR (gst_sensor_src_start);
  gstpushsrc->fill = GST_DEBUG_FUNCPTR (gst_sensor_src_fill);
  gstbasesrc->get_caps = GST_DEBUG_FUNCPTR (gst_sensorsrc_get_caps);
  gstbasesrc->stop = GST_DEBUG_FUNCPTR (gst_sensor_src_stop);

  g_object_class_install_property (gobject, PROP_SENSOR_NAME,
    g_param_spec_string ("sensor-name", "Sensor Name",
        "Name of the sensor being used, such as: accel gyro mag etc..",
        DEFAULT_PROP_SENSOR_NAME, (GParamFlags)
        (G_PARAM_CONSTRUCT | G_PARAM_READWRITE)));

  g_object_class_install_property (gobject, PROP_SAMPLE_RATE,
    g_param_spec_int ("sample-rate", "Sample Rate",
        "How often the sensor gets data in Hz", 1, 8000,
        DEFAULT_PROP_SAMPLE_RATE, (GParamFlags)
        (G_PARAM_CONSTRUCT | G_PARAM_READWRITE)));
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtisensorsrc", GST_RANK_PRIMARY,
      GST_TYPE_SENSOR_SRC);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  qtisensorsrc,
  "Vision Mezz Sensor Source",
  plugin_init,
  PACKAGE_VERSION,
  PACKAGE_LICENSE,
  PACKAGE_SUMMARY,
  PACKAGE_ORIGIN
)
