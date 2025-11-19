/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/*
* Application:
* GStreamer cameras and sensors streams.
*
* Description:
* This application creates cameras and sensors streams
*
* Usage:
* In separate console run:
*
* gst-cameras-sensors-example
*
* Help:
* gst-cameras-sensors-example --help
*/

#include <stdio.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>
#include <gst/app/gstappsrc.h>

#define S0_CAMERA_ID                0
#define S1_CAMERA_ID                1
#define LOGICAL_CAMERA_ID           4
#define S0_HIGH_OUTPUT_WIDTH        1672
#define S0_HIGH_OUTPUT_HEIGHT       1256
#define S0_LOW_OUTPUT_WIDTH         640
#define S0_LOW_OUTPUT_HEIGHT        480
#define S1_OUTPUT_WIDTH             1672
#define S1_OUTPUT_HEIGHT            804
#define S2_OUTPUT_WIDTH             1408
#define S2_OUTPUT_HEIGHT            1256
#define S3_OUTPUT_WIDTH             1408
#define S3_OUTPUT_HEIGHT            1256
#define STITCHING_OUTPUT_WIDTH      640
#define STITCHING_OUTPUT_HEIGHT     480
#define AI_NODE_QUEUE_COUNT         15
#define STITCHING_NODE_QUEUE_COUNT  20

#define AI_NODE_DETECTION_MODEL               "/data/yolov8_det_quantized.tflite"
#define AI_NODE_DETECTION_LABEL               "/data/yolov8.labels"
#define AI_NODE_S0_JPEG_OUTPUT                "/data/S0_Jpeg1672x1256.avi"
#define AI_NODE_S0_HIGH_H264_OUTPUT           "/data/S0_Avc1672x1256.mp4"
#define AI_NODE_S0_LOW_H264_OUTPUT            "/data/S0_Avc640x480.mp4"
#define AI_NODE_S1_RAW_OUTPUT                 "/data/S1_Raw1672x804_%d.raw16"
#define STITCHING_NODE_S2_JPEG_OUTPUT         "/data/S2_Jpeg1408x1256.avi"
#define STITCHING_NODE_S2_H264_OUTPUT         "/data/S2_Avc1408x1256.mp4"
#define STITCHING_NODE_S3_JPEG_OUTPUT         "/data/S3_Jpeg1408x1256.avi"
#define STITCHING_NODE_S3_H264_OUTPUT         "/data/S3_Avc1408x1256.mp4"
#define STITCHING_NODE_STITCHING_H264_OUTPUT  "/data/S2-3_Avc640x480.mp4"
/**
 * Default constants to dequantize values
 */
#define DEFAULT_CONSTANTS_YOLOV8 \
    "YOLOv8,q-offsets=<21.0, 0.0, 0.0>, \
    q-scales=<3.093529462814331, 0.00390625, 1.0>;"

typedef struct _GstAppContext GstAppContext;
typedef struct _GstSensorsData GstSensorsData;

struct _GstAppContext
{
  // Pointer to the pipeline
  GstElement *pipeline;
  // list of ai node stream plugins
  GList      *ai_plugins;
  // list of logical camera stream plugins
  GList      *sti_plugins;
  // Pointer to the mainloop
  GMainLoop  *mloop;
  GMutex     mutex;
  gboolean   exit;
  guint      pending;

  GMutex     s0_jpeg_mutex;
  gboolean   s0_jpeg;
  GstElement *s0_jpeg_appsrc;

  GstDataQueue *sensor_bufqueue;
};

struct _GstSensorsData
{
  gint temperature;
  gint altitude;
};

static GstAppContext*
gst_app_context_init ()
{
  GstAppContext *appctx = g_new0 (GstAppContext, 1);

  appctx->pipeline = NULL;
  appctx->mloop = NULL;
  appctx->ai_plugins = NULL;
  appctx->sti_plugins = NULL;
  appctx->exit = FALSE;
  appctx->s0_jpeg = FALSE;

  g_mutex_init (&appctx->s0_jpeg_mutex);
  g_mutex_init (&appctx->mutex);

  return appctx;
}

static void
gst_app_context_free (GstAppContext * appctx)
{
  g_mutex_clear (&appctx->s0_jpeg_mutex);
  g_mutex_clear (&appctx->mutex);

  if (appctx->mloop != NULL)
    g_main_loop_unref (appctx->mloop);

  g_free (appctx);
  return;
}

static void
build_pad_property (GValue * property, gint values[], gint num)
{
  GValue val = G_VALUE_INIT;
  g_value_init (&val, G_TYPE_INT);

  for (gint idx = 0; idx < num; idx++) {
    g_value_set_int (&val, values[idx]);
    gst_value_array_append_value (property, &val);
  }

  g_value_unset (&val);
}

// Handle interrupt by CTRL+C
static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  GstState state, pending;

  g_print ("\n\nReceived an interrupt signal, send EOS ...\n");

  if (!gst_element_get_state (
      appctx->pipeline, &state, &pending, GST_CLOCK_TIME_NONE)) {
    gst_printerr ("ERROR: get current state!\n");
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
    return TRUE;
  }

  if (state == GST_STATE_PLAYING) {
    gst_element_send_event (appctx->pipeline, gst_event_new_eos ());
  } else {
    g_main_loop_quit (appctx->mloop);
  }

  return TRUE;
}

// Handle state change events for the pipeline
static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, new_st, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &new_st, &pending);

  if ((new_st == GST_STATE_PAUSED) && (old == GST_STATE_READY) &&
      (pending == GST_STATE_VOID_PENDING)) {

    if (gst_element_set_state (pipeline,
            GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      gst_printerr (
          "\nPipeline doesn't want to transition to PLAYING state!\n");
      return;
    }
  }
}

// Handle warning events
static void
warning_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_warning (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);
}

// Handle error events
static void
error_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);

  g_main_loop_quit (mloop);
}

// Handle end of stream event
static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));
  g_main_loop_quit (mloop);
}

// Release elements
static void
destroy_elements (GList **plugins)
{
  GstElement * element = NULL;
  g_return_if_fail (*plugins);

  GList *list = *plugins;
  for ( ; list != NULL; list = list->next) {
    element = GST_ELEMENT_CAST (list->data);
    if (element == NULL)
        continue;
    gst_object_unref (element);
  }

  g_list_free (*plugins);
  *plugins = NULL;
}

// Unlink and remove all elements
static void
destroy_pipe (GstAppContext *appctx)
{
  GstElement * element_1 = NULL;
  GstElement * element_2 = NULL;

  g_return_if_fail (appctx->ai_plugins ||
      appctx->sti_plugins);

  if (appctx->ai_plugins) {
    element_1 = GST_ELEMENT_CAST (
        g_list_nth_data (appctx->ai_plugins, 0));
    GList *list = appctx->ai_plugins->next;

    for ( ; list != NULL; list = list->next) {
      element_2 = (GstElement *) list->data;
      gst_element_unlink (element_1, element_2);
      gst_bin_remove (GST_BIN (appctx->pipeline), element_1);
      element_1 = element_2;
    }
    gst_bin_remove (GST_BIN (appctx->pipeline), element_1);
    g_list_free (appctx->ai_plugins);
    appctx->ai_plugins = NULL;
  }

  if (appctx->sti_plugins) {
    element_1 = GST_ELEMENT_CAST (
        g_list_nth_data (appctx->sti_plugins, 0));
    GList *list = appctx->sti_plugins->next;

    for ( ; list != NULL; list = list->next) {
      element_2 = (GstElement *) list->data;
      gst_element_unlink (element_1, element_2);
      gst_bin_remove (GST_BIN (appctx->pipeline), element_1);
      element_1 = element_2;
    }
    gst_bin_remove (GST_BIN (appctx->pipeline), element_1);
    g_list_free (appctx->sti_plugins);
    appctx->sti_plugins = NULL;
  }

  gst_object_unref (appctx->pipeline);
}

static gboolean
handle_stdin_source (GIOChannel * source, GIOCondition cond, gpointer userdata)
{
  GIOStatus status = G_IO_STATUS_NORMAL;
  gchar *input = NULL;
  GstAppContext *appctx = (GstAppContext *)userdata;

  do {
    GError *error = NULL;
    status = g_io_channel_read_line (source, &input, NULL, NULL, &error);

    if ((G_IO_STATUS_ERROR == status) && (error != NULL)) {
      g_printerr ("Failed to parse command line options: %s!\n",
           GST_STR_NULL (error->message));
      g_clear_error (&error);
      return FALSE;
    } else if ((G_IO_STATUS_ERROR == status) && (NULL == error)) {
      g_printerr ("Unknown error!\n");
      return FALSE;
    }
  } while (status == G_IO_STATUS_AGAIN);

  if (input == NULL || *input == '\0') {
    g_free (input);
    return TRUE;
  }

  switch (g_ascii_tolower (input[0])) {
    case 'q':
    {
      g_print ("Reading %s\n", input);
      break;
    }
    case '1':
    {
      g_mutex_lock (&appctx->s0_jpeg_mutex);
      appctx->s0_jpeg = TRUE;
      g_mutex_unlock (&appctx->s0_jpeg_mutex);
      break;
    }
    default:
      g_print ("Reading unknown command: %s\n", input);
      break;
  }

  g_free (input);
  return TRUE;
}

static void
gst_sample_release (GstSample * sample)
{
    gst_sample_unref (sample);
#if GST_VERSION_MAJOR >= 1 && GST_VERSION_MINOR > 14
    gst_sample_set_buffer (sample, NULL);
#endif
}

static void
gst_free_queue_item (gpointer data)
{
  GstDataQueueItem *item = (GstDataQueueItem *) data;
  gst_buffer_unref (GST_BUFFER (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
                  guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

static void
sensor_buffers_task_func (gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  GstBuffer *buffer = NULL;
  static GstClockTime timestamp = 0;
  GstDataQueueItem *item = NULL;

  if (!gst_data_queue_pop (appctx->sensor_bufqueue, &item)) {
    g_print ("buffers_queue flushing\n");
    return;
  }

  buffer = gst_buffer_ref (GST_BUFFER (item->object));
  item->destroy (item);
  g_mutex_lock (&appctx->mutex);

  // Get timestamp
  timestamp = GST_BUFFER_OFFSET (buffer);
  //g_print ("Sensor timestamp: %" G_GUINT64_FORMAT "\n", timestamp);

  // For now, just release the buffer.
  gst_buffer_unref (buffer);
  g_mutex_unlock (&appctx->mutex);

  return;
}

static GstFlowReturn
new_sample_sensor (GstElement * element, gpointer userdata)
{
  GstAppContext *ctx = (GstAppContext *)userdata;
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  GstMapInfo memmap;
  guint64 timestamp = 0;

  // New sample is available, retrieve the buffer from the sink.
  g_signal_emit_by_name (element, "pull-sample", &sample);

  if (sample == NULL) {
    g_printerr ("ERROR: Pulled sample is NULL!\n");
    return GST_FLOW_ERROR;
  }

  if ((buffer = gst_sample_get_buffer (sample)) == NULL) {
    g_printerr ("ERROR: Pulled buffer is NULL!\n");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  if ((gst_buffer_get_size (buffer) == 0) &&
      GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP)) {
    gst_sample_release (sample);
    return GST_FLOW_OK;
  }

  // Increase ref of the bufffer and release the sample
  // Use the buffer for the next plugin
  gst_buffer_ref (buffer);
  gst_sample_release (sample);

  // Push the sample in the queue
  GstDataQueueItem *item = NULL;
  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (buffer);
  item->visible = TRUE;
  item->destroy = gst_free_queue_item;
  if (!gst_data_queue_push (ctx->sensor_bufqueue, item)) {
    g_printerr ("ERROR: Cannot push data to the queue!\n");
    item->destroy (item);
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
new_sample_s0_jpeg (GstElement * element, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *)userdata;
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;

  // New sample is available, retrieve the buffer from the sink.
  g_signal_emit_by_name (element, "pull-sample", &sample);

  if (sample == NULL) {
    g_printerr ("ERROR: Pulled sample is NULL!\n");
    return GST_FLOW_ERROR;
  }

  // No encode request, just release the sample.
  if (!appctx->s0_jpeg) {
    goto quit;
  }

  // JPEG encode request, push buffer to appsrc.
  if ((buffer = gst_sample_get_buffer (sample)) == NULL) {
    g_printerr ("ERROR: Pulled buffer is NULL!\n");
    goto quit;
  }

  if (!appctx->exit) {
    // Push buffer to appsrc
    g_print ("Push buffer to appsrc\n");
    GstFlowReturn ret = gst_app_src_push_buffer (
        GST_APP_SRC (appctx->s0_jpeg_appsrc), gst_buffer_ref (buffer));
    if (ret != GST_FLOW_OK) {
      g_printerr ("ERROR: gst_app_src_push_buffer!\n");
    }
  } else {
    g_print ("EOS, release buffer\n");
  }

  // Pushed one sample, reset flag.
  g_mutex_lock (&appctx->s0_jpeg_mutex);
  appctx->s0_jpeg = FALSE;
  g_mutex_unlock (&appctx->s0_jpeg_mutex);

quit:
  gst_sample_release (sample);
  return GST_FLOW_OK;
}

static gboolean
create_sensor_streams (GstAppContext *appctx)
{
  GstElement *sensorsrc0, *capsfilter0, *queue0, *appsink;
  gboolean ret = FALSE;

  sensorsrc0 = gst_element_factory_make ("qtisensorsrc", "sensorsrc0");
  queue0 = gst_element_factory_make ("queue", NULL);
  appsink = gst_element_factory_make ("appsink", NULL);

  // Check if all elements are created successfully.
  if (!sensorsrc0 || !queue0 || !appsink) {
    g_printerr ("One element could not be created. Exiting.\n");
    return FALSE;
  }

  appctx->sti_plugins = g_list_append (appctx->sti_plugins, sensorsrc0);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, queue0);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, appsink);

  g_object_set (G_OBJECT (sensorsrc0),
      "sensor-name", "accel",
      "sample-rate", 960, NULL);

  g_object_set (G_OBJECT (appsink),
      "sync", FALSE, "emit-signals", TRUE,
      "async", FALSE, "enable-last-sample", FALSE, NULL);

  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (appctx->pipeline),
     sensorsrc0, queue0, appsink, NULL);

  g_print ("Linking sensor elements...\n");
  ret = gst_element_link_many (
      sensorsrc0, queue0, appsink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }

  g_signal_connect (appsink, "new-sample",
      G_CALLBACK (new_sample_sensor), appctx);
  g_print ("All elements are linked successfully\n");

  return TRUE;

error:
  gst_bin_remove_many (GST_BIN (appctx->pipeline),
      sensorsrc0, queue0, appsink, NULL);

  return FALSE;
}

// AI Node: Create all elements and link
static gboolean
create_camera_s0_s1_streams (GstAppContext *appctx)
{
  GstElement *camsrc0, *capsfilter_high, *metamux, *overlay, *tee0,
      *appsink, *appsrc, *jpegenc_s0, *avimux, *filesink_jpeg,
      *c2venc_high, *parser_high, *mp4mux_high, *filesink_high;
  GstElement *capsfilter_low, *tee1, *mlvconverter, *mltflite, *detection,
      *capsfilter_text, *c2venc_low, *parser_low, *mp4mux_low, *filesink_low;
  GstElement *camsrc1, *capsfilter_tof, *filesink_tof;
  GstElement *queue[AI_NODE_QUEUE_COUNT] = { NULL };
  GstCaps *filtercaps;
  gchar element_name[128];
  gboolean ret = FALSE;

  if (!g_file_test (AI_NODE_DETECTION_MODEL, G_FILE_TEST_EXISTS) ||
      !g_file_test (AI_NODE_DETECTION_LABEL, G_FILE_TEST_EXISTS)) {
    g_printerr ("Could not find model or label file.\n");
    goto error;
  }

  // Create S0 streams elements.
  camsrc0 = gst_element_factory_make ("qtiqmmfsrc", "camsrc0");
  capsfilter_high = gst_element_factory_make ("capsfilter", "capsfilter-high");
  capsfilter_low = gst_element_factory_make ("capsfilter", "capsfilter-low");
  metamux = gst_element_factory_make ("qtimetamux", "metatux");
  overlay = gst_element_factory_make ("qtioverlay", "overlay");
  tee0 = gst_element_factory_make ("tee", "tee0");
  appsink = gst_element_factory_make ("appsink", "appsink-s0-jpeg");
  appsrc = gst_element_factory_make ("appsrc", "appsrc-s0-jpeg");
  jpegenc_s0 = gst_element_factory_make ("qtijpegenc", "jpegenc-s0");
  avimux = gst_element_factory_make ("avimux", "avimux-s0");
  filesink_jpeg = gst_element_factory_make ("filesink", "filesink-jpeg");
  c2venc_high = gst_element_factory_make ("qtic2venc", "c2venc-high");
  parser_high = gst_element_factory_make ("h264parse", "parser-high");
  mp4mux_high = gst_element_factory_make ("mp4mux", "mp4mux-high");
  filesink_high = gst_element_factory_make ("filesink", "filesink-high");

  tee1 = gst_element_factory_make ("tee", "tee1");
  mlvconverter = gst_element_factory_make ("qtimlvconverter", "mlvconverter");
  mltflite = gst_element_factory_make ("qtimltflite", "mltflite");
  detection = gst_element_factory_make ("qtimlvdetection", "detection");
  capsfilter_text = gst_element_factory_make ("capsfilter", "capsfilter-text");
  c2venc_low = gst_element_factory_make ("qtic2venc", "c2venc-low");
  parser_low = gst_element_factory_make ("h264parse", "parser-low");
  mp4mux_low = gst_element_factory_make ("mp4mux", "mp4mux-low");
  filesink_low = gst_element_factory_make ("filesink", "filesink-low");

  // Create S1 stream elements.
  camsrc1 = gst_element_factory_make ("qtiqmmfsrc", "camsrc1");
  capsfilter_tof = gst_element_factory_make ("capsfilter", "capsfilter-tof");
  filesink_tof = gst_element_factory_make ("multifilesink", "filesink-tof");

  // Create queue to decouple the processing on sink and source pad.
  for (gint i = 0; i < AI_NODE_QUEUE_COUNT; i++) {
    snprintf (element_name, 127, "ai-queue-%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create %s\n", element_name);
      destroy_elements (&appctx->ai_plugins);
      return FALSE;
    }
    appctx->ai_plugins = g_list_append (appctx->ai_plugins, queue[i]);
  }

  // Append all elements in a list.
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, camsrc0);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, capsfilter_high);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, capsfilter_low);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, metamux);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, overlay);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, tee0);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, appsink);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, appsrc);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, jpegenc_s0);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, avimux);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, filesink_jpeg);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, c2venc_high);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, parser_high);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, mp4mux_high);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, filesink_high);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, tee1);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, mlvconverter);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, mltflite);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, detection);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, capsfilter_text);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, c2venc_low);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, parser_low);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, mp4mux_low);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, filesink_low);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, camsrc1);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, capsfilter_tof);
  appctx->ai_plugins = g_list_append (appctx->ai_plugins, filesink_tof);

  // Check if all elements are created successfully.
  if (!camsrc0 || !capsfilter_high || !capsfilter_low || !metamux ||
      !overlay || !tee0 || !appsink || !appsrc || !jpegenc_s0 ||
      !avimux || !filesink_jpeg || !c2venc_high || !parser_high ||
      !mp4mux_high || !filesink_high || !tee1 || !mlvconverter ||
      !mltflite || !detection || !capsfilter_text || !c2venc_low ||
      !parser_low || !mp4mux_low || !filesink_low  || !camsrc1 ||
      !capsfilter_tof || !filesink_tof) {
    g_printerr ("One element could not be created. Exiting.\n");
    destroy_elements (&appctx->ai_plugins);
    return FALSE;
  }

  g_object_set (G_OBJECT (camsrc0), "camera", S0_CAMERA_ID, NULL);
  // Configure the stream caps.
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, S0_HIGH_OUTPUT_WIDTH,
      "height", G_TYPE_INT, S0_HIGH_OUTPUT_HEIGHT,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter_high), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

 // Set appsrc properties.
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, S0_HIGH_OUTPUT_WIDTH,
      "height", G_TYPE_INT, S0_HIGH_OUTPUT_HEIGHT,
      "framerate", GST_TYPE_FRACTION, 1, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (appsrc), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  g_object_set (G_OBJECT (appsrc),
      "stream-type", 0, // GST_APP_STREAM_TYPE_STREAM
      "format", GST_FORMAT_TIME,
      "is-live", TRUE,
      NULL);

  // Configure the stream caps.
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, S0_LOW_OUTPUT_WIDTH,
      "height", G_TYPE_INT, S0_LOW_OUTPUT_HEIGHT,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter_low), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  filtercaps = gst_caps_new_simple ("text/x-raw",
      "format", G_TYPE_STRING, "utf8", NULL);
  g_object_set (G_OBJECT (capsfilter_text), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  g_object_set (G_OBJECT (camsrc1), "camera", S1_CAMERA_ID, NULL);
  // Configure the stream caps.
  filtercaps = gst_caps_new_simple ("video/x-bayer",
      "format", G_TYPE_STRING, "rggb",
      "bpp", G_TYPE_STRING, "16",
      "width", G_TYPE_INT, 4056,
      "height", G_TYPE_INT, 3040,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  g_object_set (G_OBJECT (capsfilter_tof), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Set ML properties.
  g_object_set (G_OBJECT (mltflite),
      "model", AI_NODE_DETECTION_MODEL, "delegate", 5, NULL);
  g_object_set (G_OBJECT (detection),
      "threshold", 75.0, "results", 10, "module", 7,
      "labels", AI_NODE_DETECTION_LABEL, NULL);
  g_object_set (G_OBJECT (detection), "constants",
      DEFAULT_CONSTANTS_YOLOV8, NULL);

  // Set encoder properties.
  g_object_set (G_OBJECT (c2venc_high),
      "target-bitrate", 6000000, "control-rate", 3,
      "min-quant-i-frames", 20, "min-quant-p-frames", 20,
      "max-quant-p-frames", 30, "max-quant-i-frames", 30,
      "quant-i-frames", 25, "quant-p-frames", 25, NULL);
  g_object_set (G_OBJECT (c2venc_low),
      "target-bitrate", 6000000, "control-rate", 3,
      "min-quant-i-frames", 20, "min-quant-p-frames", 20,
      "max-quant-p-frames", 30, "max-quant-i-frames", 30,
      "quant-i-frames", 25, "quant-p-frames", 25, NULL);

  // Set appsink properties.
  g_object_set (G_OBJECT (appsink),
      "emit-signals", TRUE, "async", FALSE, "sync", FALSE, NULL);

  // Set filesink properties.
  g_object_set (G_OBJECT (filesink_jpeg),
      "location", AI_NODE_S0_JPEG_OUTPUT,
      "enable-last-sample", FALSE, "async", FALSE, NULL);
  g_object_set (G_OBJECT (filesink_high),
      "location", AI_NODE_S0_HIGH_H264_OUTPUT,
      "enable-last-sample", FALSE, NULL);
  g_object_set (G_OBJECT (filesink_low),
      "location", AI_NODE_S0_LOW_H264_OUTPUT,
      "enable-last-sample", FALSE, NULL);
  g_object_set (G_OBJECT (filesink_tof),
      "location", AI_NODE_S1_RAW_OUTPUT,
      "max-files", 5, NULL);

  // Add elements to the pipeline and link them.
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (appctx->pipeline),
      camsrc0, capsfilter_high, capsfilter_low, metamux, overlay, tee0,
      appsink, appsrc, jpegenc_s0, avimux, filesink_jpeg, c2venc_high,
      parser_high, mp4mux_high, filesink_high, tee1, mlvconverter, mltflite,
      detection, capsfilter_text, c2venc_low, parser_low, mp4mux_low,
      filesink_low, camsrc1, capsfilter_tof, filesink_tof, NULL);

  for (gint i = 0; i < AI_NODE_QUEUE_COUNT; i++)
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);

  g_print ("Linking S0 jpeg encoder elements...\n");
  // Linking the high video jpeg encode stream.
  ret = gst_element_link_many (
      camsrc0, capsfilter_high, queue[0], metamux, queue[1], overlay, tee0,
      appsink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }
  ret = gst_element_link_many (
      appsrc, queue[2], jpegenc_s0,
      queue[3], avimux, filesink_jpeg, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }
  appctx->s0_jpeg_appsrc = appsrc;
  g_signal_connect (appsink, "new-sample",
      G_CALLBACK (new_sample_s0_jpeg), appctx);

  g_print ("Linking S0 high video encoder elements...\n");
  // Linking S0 high video encode stream.
  ret = gst_element_link_many (
      tee0, c2venc_high, queue[4], parser_high,
      mp4mux_high, queue[5], filesink_high, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }

 g_print ("Linking S0 low video detection elements...\n");
  // Linking video detection stream.
  ret = gst_element_link_many (
      camsrc0, capsfilter_low, queue[6], tee1, queue[7], mlvconverter,
       mltflite, queue[8], detection, capsfilter_text, queue[9], metamux, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }

  g_print ("Linking S0 low video encoder elements...\n");
  // Linking S0 low video encode stream.
  ret = gst_element_link_many (
      tee1, queue[10], c2venc_low, queue[11], parser_low,
      queue[12], mp4mux_low, filesink_low, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }

  g_print ("Linking S1 mono video elements...\n");
  // Linking the mono stream.
  ret = gst_element_link_many (
      camsrc1, capsfilter_tof, queue[13], filesink_tof, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }

  g_print ("AI node: all elements are linked successfully\n");

  return TRUE;

error:
  // destroy pipe will be called by main function.
  return FALSE;
}

// 360 Node: Create all elements and link
static gboolean
create_logical_camera_streams (GstAppContext *appctx)
{
  GstElement *camsrc_logical, *capsfilter_s2, *tee2, *videorate2,
      *capsfilter_s2_jpeg,*jpegenc_s2, *avimux_s2, *filesink_s2jpeg,
      *c2venc_s2, *parser_s2, *mp4mux_s2, *filesink_s2avc;
  GstElement *capsfilter_s3, *tee3, *videorate3, *capsfilter_s3_jpeg,
      *jpegenc_s3, *avimux_s3, *filesink_s3jpeg, *c2venc_s3, *parser_s3,
      *mp4mux_s3, *filesink_s3avc;
  GstElement *stitch, *tcapsfilter, *vtrans, *composer, *capsfilter, *c2venc,
      *parser, *mp4mux, *filesink;
  GstElement *queue[STITCHING_NODE_QUEUE_COUNT] = {NULL};
  GstPad *s2pad, *s3pad;
  GstCaps *filtercaps;
  gchar element_name[128];
  gboolean ret = FALSE;

  // Create S2 stream elements.
  camsrc_logical = gst_element_factory_make ("qtiqmmfsrc", "camsrc-logical");
  capsfilter_s2 = gst_element_factory_make ("capsfilter", "capsfilter-s2");
  tee2 = gst_element_factory_make ("tee", "tee2");
  videorate2 = gst_element_factory_make ("videorate", "videorate2");
  capsfilter_s2_jpeg = gst_element_factory_make ("capsfilter", NULL);
  jpegenc_s2 = gst_element_factory_make ("qtijpegenc", "jpegenc-s2");
  avimux_s2 = gst_element_factory_make ("avimux", "avimux-s2");
  filesink_s2jpeg = gst_element_factory_make ("filesink", "filesink-s2jpeg");
  c2venc_s2 = gst_element_factory_make ("qtic2venc", "c2venc-s2");
  parser_s2 = gst_element_factory_make ("h264parse", "parser-s2");
  mp4mux_s2 = gst_element_factory_make ("mp4mux", "mp4mux-s2");
  filesink_s2avc = gst_element_factory_make ("filesink", "filesink-s2avc");

  // Create S3 stream elements.
  capsfilter_s3 = gst_element_factory_make ("capsfilter", "capsfilter-s3");
  tee3 = gst_element_factory_make ("tee", "tee3");
  videorate3 = gst_element_factory_make ("videorate", "videorate3");
  capsfilter_s3_jpeg = gst_element_factory_make ("capsfilter", NULL);
  jpegenc_s3 = gst_element_factory_make ("qtijpegenc", "jpegenc-s3");
  avimux_s3 = gst_element_factory_make ("avimux", "avimux-s3");
  filesink_s3jpeg = gst_element_factory_make ("filesink", "filesink30");
  c2venc_s3 = gst_element_factory_make ("qtic2venc", "c2venc-s3");
  parser_s3 = gst_element_factory_make ("h264parse", "parser-s3");
  mp4mux_s3 = gst_element_factory_make ("mp4mux", "mp4mux-s3");
  filesink_s3avc = gst_element_factory_make ("filesink", "filesink-s3avc");

  //Create stitching stream elements.
  stitch = gst_element_factory_make ("qtisamplestitching", "stitch");
  tcapsfilter = gst_element_factory_make ("capsfilter", "stitch-capsfilter5");
  vtrans = gst_element_factory_make ("qtivtransform", "vtransform");
  capsfilter = gst_element_factory_make ("capsfilter", "stitch-capsfilter4");
  c2venc = gst_element_factory_make ("qtic2venc", "c2venc");
  parser = gst_element_factory_make ("h264parse", "parser");
  mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");
  filesink = gst_element_factory_make ("filesink", "filesink");

  // Create queue to decouple the processing on sink and source pad
  for (gint i = 0; i < STITCHING_NODE_QUEUE_COUNT; i++) {
    snprintf (element_name, 127, "stitch-queue-%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create %s\n", element_name);
      destroy_elements (&appctx->sti_plugins);
      return FALSE;
    }
    appctx->sti_plugins = g_list_append (appctx->sti_plugins, queue[i]);
  }

  // Append all elements in a list.
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, camsrc_logical);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, capsfilter_s2);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, tee2);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, videorate2);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, capsfilter_s2_jpeg);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, jpegenc_s2);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, filesink_s2jpeg);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, avimux_s2);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, c2venc_s2);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, parser_s2);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, mp4mux_s2);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, filesink_s2avc);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, capsfilter_s3);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, tee3);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, videorate3);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, capsfilter_s3_jpeg);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, jpegenc_s3);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, avimux_s3);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, filesink_s3jpeg);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, c2venc_s3);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, parser_s3);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, mp4mux_s3);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, filesink_s3avc);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, stitch);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, tcapsfilter);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, vtrans);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, capsfilter);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, c2venc);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, parser);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, mp4mux);
  appctx->sti_plugins = g_list_append (appctx->sti_plugins, filesink);

  // Check if all elements are created successfully.
  if (!camsrc_logical || !capsfilter_s2 || !tee2 || !videorate2 ||
      !capsfilter_s2_jpeg  || !jpegenc_s2 || !avimux_s2 || !filesink_s2jpeg ||
      !c2venc_s2 || !parser_s2 || !mp4mux_s2 || !filesink_s2avc ||
      !capsfilter_s3 || !tee3 || !videorate3 || !capsfilter_s3_jpeg ||
      !jpegenc_s3 || !avimux_s3 || !filesink_s3jpeg || !c2venc_s3 ||
      !parser_s3 || !mp4mux_s3 || !filesink_s3avc || !stitch || !tcapsfilter ||
      !vtrans || !capsfilter || !c2venc || !parser || !mp4mux || !filesink) {
    g_printerr ("One element could not be created. Exiting.\n");
    destroy_elements (&appctx->sti_plugins);
    return FALSE;
  }

  // Set logical camera id, it will collect images from sensor2 and sensor3.
  g_object_set (G_OBJECT (camsrc_logical),
      "camera", LOGICAL_CAMERA_ID, NULL);

  // Configure the stream caps.
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, S2_OUTPUT_WIDTH,
      "height", G_TYPE_INT, S2_OUTPUT_HEIGHT,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter_s2), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Configure the stream caps.
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, S3_OUTPUT_WIDTH,
      "height", G_TYPE_INT, S3_OUTPUT_HEIGHT,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter_s3), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Configure caps for JPEG encode stream.
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "framerate", GST_TYPE_FRACTION, 1, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter_s2_jpeg), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Configure caps for JPEG encode stream.
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "framerate", GST_TYPE_FRACTION, 1, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter_s3_jpeg), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Configure the stream caps.
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, S3_OUTPUT_WIDTH * 2,
      "height", G_TYPE_INT, S3_OUTPUT_HEIGHT,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (tcapsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Configure the stream caps.
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, STITCHING_OUTPUT_WIDTH,
      "height", G_TYPE_INT, STITCHING_OUTPUT_HEIGHT,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // Set vtransform properties.
  g_object_set (G_OBJECT (vtrans), "engine", 1, NULL);

   // Set encoder properties.
  g_object_set (G_OBJECT (c2venc_s2),
      "target-bitrate", 6000000, "control-rate", 3,
      "min-quant-i-frames", 20, "min-quant-p-frames", 20,
      "max-quant-p-frames", 30, "max-quant-i-frames", 30,
      "quant-i-frames", 25, "quant-p-frames", 25, NULL);

  g_object_set (G_OBJECT (c2venc_s3),
      "target-bitrate", 6000000, "control-rate", 3,
      "min-quant-i-frames", 20, "min-quant-p-frames", 20,
      "max-quant-p-frames", 30, "max-quant-i-frames", 30,
      "quant-i-frames", 25, "quant-p-frames", 25, NULL);

  g_object_set (G_OBJECT (c2venc),
      "target-bitrate", 6000000, "control-rate", 3,
      "min-quant-i-frames", 20, "min-quant-p-frames", 20,
      "max-quant-p-frames", 30, "max-quant-i-frames", 30,
      "quant-i-frames", 25, "quant-p-frames", 25, NULL);

  // Set filesink properties.
  g_object_set (G_OBJECT (filesink_s2jpeg),
      "location", STITCHING_NODE_S2_JPEG_OUTPUT,
      "enable-last-sample", FALSE, NULL);
  g_object_set (G_OBJECT (filesink_s2avc),
      "location", STITCHING_NODE_S2_H264_OUTPUT,
      "enable-last-sample", FALSE, NULL);
  g_object_set (G_OBJECT (filesink_s3jpeg),
      "location", STITCHING_NODE_S3_JPEG_OUTPUT,
      "enable-last-sample", FALSE, NULL);
  g_object_set (G_OBJECT (filesink_s3avc),
      "location", STITCHING_NODE_S3_H264_OUTPUT,
      "enable-last-sample", FALSE, NULL);
  g_object_set (G_OBJECT (filesink),
      "location", STITCHING_NODE_STITCHING_H264_OUTPUT,
      "enable-last-sample", FALSE, NULL);

  // Add elements to the pipeline and link them
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (appctx->pipeline),
      camsrc_logical, capsfilter_s2, tee2, videorate2, capsfilter_s2_jpeg,
      jpegenc_s2, avimux_s2, filesink_s2jpeg, c2venc_s2, parser_s2,
      mp4mux_s2, filesink_s2avc, capsfilter_s3, tee3, videorate3,
      capsfilter_s3_jpeg, jpegenc_s3, avimux_s3, filesink_s3jpeg, c2venc_s3,
      parser_s3, mp4mux_s3, filesink_s3avc, stitch,tcapsfilter, vtrans, capsfilter,
      c2venc, parser, mp4mux, filesink, NULL);

  for (gint i = 0; i < STITCHING_NODE_QUEUE_COUNT; i++)
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);

  g_print ("Linking S2 jpeg encoder elements...\n");
  ret = gst_element_link_many (
      camsrc_logical, capsfilter_s2, queue[0], tee2, queue[1], videorate2,
      capsfilter_s2_jpeg, queue[2], jpegenc_s2, queue[3], avimux_s2,
      filesink_s2jpeg, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }

  // Set video_0 to video and select S2 as data source.
  s2pad = gst_element_get_static_pad (camsrc_logical, "video_0");
  g_return_val_if_fail (s2pad != NULL, FALSE);
  g_object_set (G_OBJECT (s2pad),
      "type", 0, "logical-stream-type", 0, NULL);

  g_print ("Linking S2 video encoder elements...\n");
  ret = gst_element_link_many (
      tee2, queue[4], c2venc_s2, queue[5], parser_s2,
      mp4mux_s2, queue[6], filesink_s2avc, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }

  g_print ("Linking S3 jpeg encoder elements...\n");
  ret = gst_element_link_many (
      camsrc_logical, capsfilter_s3, queue[7], tee3, queue[8], videorate3,
      capsfilter_s3_jpeg, queue[9], jpegenc_s3, queue[10], avimux_s3,
      filesink_s3jpeg, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }

  // Set video_1 to video and select S3 as data source.
  s3pad = gst_element_get_static_pad (camsrc_logical, "video_1");
  g_return_val_if_fail (s3pad != NULL, FALSE);
  g_object_set (G_OBJECT (s3pad),
      "type", 0, "logical-stream-type", 1, NULL);

  g_print ("Linking S3 video encoder elements...\n");
  ret = gst_element_link_many (
      tee3, queue[11], c2venc_s3, queue[12], parser_s3,
      queue[13], mp4mux_s3, filesink_s3avc, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }

  g_print ("Linking S2/S3 stiching video encoder elements...\n");
  ret = gst_element_link_many (
      tee2, queue[14], stitch,tcapsfilter, queue[15], vtrans, capsfilter,
      queue[16], c2venc, queue[17], parser, queue[18], mp4mux, filesink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }

  ret = gst_element_link_many (tee3, queue[19], stitch, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked. Exiting.\n");
    goto error;
  }

  g_print ("Stitching node: all elements are linked successfully\n");

  return TRUE;

error:
  // destroy pipe will be called by main function.
  return FALSE;
}

gint
main (gint argc, gchar * argv[])
{
  GIOChannel *iostdin = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0, stdin_watch_id = 0;
  GstElement *pipeline = NULL;
  gboolean ret = FALSE;
  GstAppContext* appctx;
  GstTask *bufferstask = NULL;
  GRecMutex bufferslock;

  // Set env for EGL.
  g_setenv ("XDG_RUNTIME_DIR", "/run/user/root", FALSE);
  g_setenv ("WAYLAND_DISPLAY", "wayland-1", FALSE);

  appctx = gst_app_context_init ();
  g_return_val_if_fail (appctx != NULL, -1);
  // Initialize GST library.
  gst_init (&argc, &argv);

  // Create the pipeline.
  pipeline = gst_pipeline_new ("gst-four-cameras-app");
  if (!pipeline) {
    g_printerr ("failed to create pipeline.\n");
    goto cleanup;
  }

  appctx->pipeline = pipeline;

  // Build AI node pipeline.
  ret = create_camera_s0_s1_streams (appctx);
  if (!ret) {
    g_printerr ("failed to create AI node streams.\n");
    goto cleanup;
  }
  // Build logical camera pipeline.
  ret = create_logical_camera_streams (appctx);
  if (!ret) {
    g_printerr ("failed to create logical camera streams.\n");
    goto cleanup;
  }
  // Build sensor pipeline.
  ret = create_sensor_streams (appctx);
  if (!ret) {
    g_printerr ("failed to create sensor streams.\n");
    goto cleanup;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    goto cleanup;
  }
  appctx->mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    goto cleanup;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  // Create IO channel from the stdin stream.
  if ((iostdin = g_io_channel_unix_new (fileno (stdin))) == NULL) {
    g_printerr ("ERROR: Failed to initialize Main loop!\n");
    goto cleanup;
  }

  // Register handing function with the main loop for stdin channel data.
  stdin_watch_id = g_io_add_watch (iostdin,
      GIOCondition (G_IO_IN | G_IO_PRI), handle_stdin_source, appctx);
  g_io_channel_unref (iostdin);

  // Create sensor buffers queue
  appctx->sensor_bufqueue =
      gst_data_queue_new (queue_is_full_cb, NULL, NULL, mloop);
  gst_data_queue_set_flushing (appctx->sensor_bufqueue, FALSE);

  // Create sensor buffer queue task
  g_rec_mutex_init (&bufferslock);
  bufferstask =
      gst_task_new (sensor_buffers_task_func, appctx, NULL);
  gst_task_set_lock (bufferstask, &bufferslock);

  // Start sensor buffer queue task
  gst_task_start (bufferstask);

  g_print ("Setting pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PAUSED state!\n");
      break;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live and does not need PREROLL.\n");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Pipeline is PREROLLING ...\n");
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("Pipeline state change was successful\n");
      break;
  }

  g_print ("g_main_loop_run\n");
  g_main_loop_run (mloop);
  g_print ("g_main_loop_run ends\n");

  // Disable sensor buffers queue
  gst_data_queue_set_flushing (appctx->sensor_bufqueue, TRUE);

  // Stop tasks
  gst_task_stop (bufferstask);

  // Make sure task is not running.
  g_rec_mutex_lock (&bufferslock);
  g_rec_mutex_unlock (&bufferslock);

  gst_task_join (bufferstask);
  g_rec_mutex_clear (&bufferslock);
  gst_object_unref (bufferstask);

  g_source_remove (intrpt_watch_id);
  g_source_remove (stdin_watch_id);

  g_print ("Setting pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

cleanup:
  g_print ("Destory pipeline\n");
  destroy_pipe (appctx);
  gst_app_context_free (appctx);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
