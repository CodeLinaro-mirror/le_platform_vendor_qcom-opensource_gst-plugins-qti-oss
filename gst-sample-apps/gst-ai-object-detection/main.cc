/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * AI based Object Detection on Live stream.
 *
 * Description:
 * The application takes live video stream from camera and gives same to
 * Yolo models  for object detection and display preview with overlayed
<<<<<<< HEAD
 * AI Model outout (Labels & Bounding Boxes).
 *
 * Pipeline for Gstreamer:
 * qtiqmmfsrc (Camera) -> main_capsfilter -> tee (SPLIT)
 *     | tee -> qtivcomposer
 *     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *     qtivcomposer (COMPOSITION) -> waylandsink (Display)
=======
 * AI Model output (Labels & Bounding Boxes).
 *
 * Pipeline for Gstreamer:
 * qtiqmmfsrc (Camera) -> qmmfsrc_caps -> qtivtransform -> tee (SPLIT)
 *     | tee -> qtivcomposer
 *     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *     qtivcomposer (COMPOSITION) -> fpsdisplaysink (Display)
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
 *     Pre process: qtimlvconverter
 *     ML Framework: qtimlsnpe/qtimltflite
 *     Post process: qtimlvdetection -> detection_filter
 */

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>

#include "include/gst_sample_apps_utils.h"

/**
 * Default models and labels path, if not provided by user
 */
#define DEFAULT_SNPE_YOLOV5_MODEL "/opt/yolov5.dlc"
#define DEFAULT_YOLOV5_LABELS "/opt/yolov5.labels"
#define DEFAULT_SNPE_YOLOV8_MODEL "/opt/yolov8.dlc"
#define DEFAULT_YOLOV8_LABELS "/opt/yolov8.labels"
#define DEFAULT_SNPE_YOLONAS_MODEL "/opt/yolonas.dlc"
#define DEFAULT_YOLONAS_LABELS "/opt/yolonas.labels"

/**
<<<<<<< HEAD
 * Default setting of camera output resolution, Scaling of camera output
=======
 * Default settings of camera output resolution, Scaling of camera output
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
 * will be done in qtimlvconverter based on model input
 */
#define DEFAULT_CAMERA_OUTPUT_WIDTH 1280
#define DEFAULT_CAMERA_OUTPUT_HEIGHT 720
#define DEFAULT_CAMERA_FRAME_RATE 30

/**
 * Number of Queues used for buffer caching between elements
 */
#define QUEUE_COUNT 7

/**
 * Create GST pipeline: has 3 main steps
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Pointer.
 * @param yolo_model_type Type of Yolo Model variant.
 * @param model_path Location of Model Container.
 * @param labels_path Location of Model Labels.
 */
static gboolean
<<<<<<< HEAD
create_pipe (GstAppContext * appctx, YoloModelType model_type,
    const char * model_path, const char * labels_path)
{
  GstElement *qtiqmmfsrc, *main_capsfilter, *queue[QUEUE_COUNT];
  GstElement *tee, *qtimlvconverter, *qtimlelement;
  GstElement *qtimlvdetection, *detection_filter;
  GstElement *qtivcomposer, *waylandsink;
  GstCaps *pad_filter, *filtercaps;
  GValue layers = G_VALUE_INIT;
  GValue value = G_VALUE_INIT;
  gint width = DEFAULT_CAMERA_OUTPUT_WIDTH;
  gint height = DEFAULT_CAMERA_OUTPUT_HEIGHT;
  gint framerate = DEFAULT_CAMERA_FRAME_RATE;
  gint module_enum;
  gchar element_name[128];
  gboolean ret = FALSE;

  // 1. Create the elements or Plugins

  // get live camera stream using qtiqmmfsrc plugin
=======
create_pipe (GstAppContext * appctx, GstYoloModelType model_type,
    const gchar * model_path, const gchar * labels_path)
{
  GstElement *qtiqmmfsrc, *qmmfsrc_caps, *qtivtransform, *queue[QUEUE_COUNT];
  GstElement *tee, *qtimlvconverter, *qtimlelement;
  GstElement *qtimlvdetection, *detection_filter;
  GstElement *qtivcomposer, *fpsdisplaysink, *waylandsink;
  GstCaps *pad_filter, *filtercaps;
  GValue layers = G_VALUE_INIT;
  GValue value = G_VALUE_INIT;
  gboolean ret = FALSE;
  gchar element_name[128];
  gint width = DEFAULT_CAMERA_OUTPUT_WIDTH;
  gint height = DEFAULT_CAMERA_OUTPUT_HEIGHT;
  gint framerate = DEFAULT_CAMERA_FRAME_RATE;
  gint module_id;

  // 1. Create the elements or Plugins
  // Create qtiqmmfsrc plugin for camera stream
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  if (!qtiqmmfsrc) {
    g_printerr ("Failed to create qtiqmmfsrc\n");
    return FALSE;
  }

  // Use capsfilter to define the camera output settings
<<<<<<< HEAD
  main_capsfilter = gst_element_factory_make ("capsfilter", "main_capsfilter");
  if (!main_capsfilter) {
    g_printerr ("Failed to create main_capsfilter\n");
    return FALSE;
  }

  // Creating queue to decouple the processing on sink and source pad.
  for (int i = 0; i < QUEUE_COUNT; i++) {
=======
  qmmfsrc_caps = gst_element_factory_make ("capsfilter", "qmmfsrc_caps");
  if (!qmmfsrc_caps) {
    g_printerr ("Failed to create qmmfsrc_caps\n");
    return FALSE;
  }

  // Create qtivtransform to convert UBWC Buffers to Non-UBWC buffers
  // for fpsdisplaysink
  qtivtransform = gst_element_factory_make ("qtivtransform",
      "qtivtransform");
  if (!qtivtransform) {
    g_printerr ("Failed to create qtivtransform\n");
    return FALSE;
  }

  // Create queue to decouple the processing on sink and source pad.
  for (gint i = 0; i < QUEUE_COUNT; i++) {
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
    snprintf (element_name, 127, "queue-%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create queue %d\n", i);
      return FALSE;
    }
  }

  // Use tee to send same data buffer
  // one for AI inferencing, one for Display composition
  tee = gst_element_factory_make ("tee", "tee");
  if (!tee) {
    g_printerr ("Failed to create tee\n");
    return FALSE;
  }

<<<<<<< HEAD
  // Creating qtimlvconverter for Input preprocessing
=======
  // Create qtimlvconverter for Input preprocessing
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
  qtimlvconverter = gst_element_factory_make ("qtimlvconverter",
      "qtimlvconverter");
  if (!qtimlvconverter) {
    g_printerr ("Failed to create qtimlvconverter\n");
    return FALSE;
  }

<<<<<<< HEAD
  // Creating the ML inferencing plugin
=======
  // Create the ML inferencing plugin
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
  qtimlelement = gst_element_factory_make ("qtimlsnpe", "qtimlsnpe");
  if (!qtimlelement) {
    g_printerr ("Failed to create qtimlelement\n");
    return FALSE;
  }

<<<<<<< HEAD
  // Creating plugin for ML postprocessing for object detection
=======
  // Create plugin for ML postprocessing for object detection
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
  qtimlvdetection = gst_element_factory_make ("qtimlvdetection",
      "qtimlvdetection");
  if (!qtimlvdetection) {
    g_printerr ("Failed to create qtimlvdetection\n");
    return FALSE;
  }

  // Composer to combine camera output with ML post proc output
  qtivcomposer = gst_element_factory_make ("qtivcomposer", "qtivcomposer");
  if (!qtivcomposer) {
    g_printerr ("Failed to create qtivcomposer\n");
    return FALSE;
  }

  // Used to negotiate between ML post proc o/p and qtivcomposer
  detection_filter = gst_element_factory_make ("capsfilter", "detection_filter");
  if (!detection_filter) {
    g_printerr ("Failed to create detection_filter\n");
    return FALSE;
  }

<<<<<<< HEAD
  // Creating Wayland compositor to render output on Display
=======
  // Create Wayland compositor to render output on Display
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
  if (!waylandsink) {
    g_printerr ("Failed to create waylandsink \n");
    return FALSE;
  }

<<<<<<< HEAD
  // 1.1 Append all elements in a list for cleanup
  appctx->plugins = NULL;
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmfsrc);
  appctx->plugins = g_list_append (appctx->plugins, main_capsfilter);
=======
  // Create fpsdisplaysink to display the current and
  // average framerate as a text overlay
  fpsdisplaysink = gst_element_factory_make ("fpsdisplaysink", "fpsdisplaysink");
  if (!fpsdisplaysink ) {
    g_printerr ("Failed to create fpsdisplaysink\n");
    return FALSE;
  }

  // 1.1 Append all elements in a list for cleanup
  appctx->plugins = NULL;
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmfsrc);
  appctx->plugins = g_list_append (appctx->plugins, qmmfsrc_caps);
  appctx->plugins = g_list_append (appctx->plugins, qtivtransform );
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
  appctx->plugins = g_list_append (appctx->plugins, tee);
  appctx->plugins = g_list_append (appctx->plugins, qtimlvconverter);
  appctx->plugins = g_list_append (appctx->plugins, qtimlelement);
  appctx->plugins = g_list_append (appctx->plugins, qtimlvdetection);
  appctx->plugins = g_list_append (appctx->plugins, detection_filter);
  appctx->plugins = g_list_append (appctx->plugins, qtivcomposer);
<<<<<<< HEAD
  appctx->plugins = g_list_append (appctx->plugins, waylandsink);

  for (int i = 0; i < QUEUE_COUNT; i++) {
=======
  appctx->plugins = g_list_append (appctx->plugins, fpsdisplaysink);

  for (gint i = 0; i < QUEUE_COUNT; i++) {
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
    appctx->plugins = g_list_append (appctx->plugins, queue[i]);
  }

  // 2. Set properties for all GST plugin elements
<<<<<<< HEAD

  // 2.1 Set the capabilities of camera plugin output
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, width, "height", G_TYPE_INT, height,
=======
  // 2.1 Set the capabilities of camera plugin output
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
      "framerate", GST_TYPE_FRACTION, framerate, 1,
      "compression", G_TYPE_STRING, "ubwc", NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
<<<<<<< HEAD
  g_object_set (G_OBJECT (main_capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // 2.2 Selecting the HW to DSP for model inferencing using delegate property
=======
  g_object_set (G_OBJECT (qmmfsrc_caps), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // 2.2 Select the HW to DSP for model inferencing using delegate property
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
  g_object_set (G_OBJECT (qtimlelement), "model", model_path,
      "delegate", GST_ML_SNPE_DELEGATE_DSP, NULL);

  // 2.3 Set properties for ML postproc plugins- module, layers, threshold
  g_value_init (&layers, GST_TYPE_ARRAY);
  g_value_init (&value, G_TYPE_STRING);
  switch (model_type) {
    // Set Yolov5 specific settings
<<<<<<< HEAD
    case YOLO_TYPE_V5:
=======
    case GST_YOLO_TYPE_V5:
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
      g_object_set (G_OBJECT (qtimlelement), "model", model_path, NULL);
      g_value_set_string (&value, "Conv_198");
      gst_value_array_append_value (&layers, &value);
      g_value_set_string (&value, "Conv_232");
      gst_value_array_append_value (&layers, &value);
      g_value_set_string (&value, "Conv_266");
      gst_value_array_append_value (&layers, &value);
      g_object_set_property (G_OBJECT (qtimlelement), "layers", &layers);

      // Get enum value of module property from qtimlvdetection plugin
<<<<<<< HEAD
      module_enum = get_enum_value (qtimlvdetection, "module" , "yolov5");
      if (module_enum != -1) {
        g_object_set (G_OBJECT (qtimlvdetection), "module", module_enum, NULL);
      } else {
        g_printerr ("Module \"yolov5s\" is not available in qtimlvdetection\n");
=======
      module_id = get_enum_value (qtimlvdetection, "module", "yolov5");
      if (module_id != -1) {
        g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
      } else {
        g_printerr ("Module yolov5s is not available in qtimlvdetection\n");
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
        goto error;
      }
      g_object_set (G_OBJECT (qtimlvdetection), "labels", labels_path, NULL);

      // Set qtimlvdetection properties
      g_object_set (G_OBJECT (qtimlvdetection), "threshold", 40.0, NULL);
      g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
      break;

    // Set Yolov8 specific settings
<<<<<<< HEAD
    case YOLO_TYPE_V8:
      g_object_set (G_OBJECT (qtimlelement), "model", model_path, NULL);
      g_value_set_string (&value, "/model.22/Sigmoid");
      gst_value_array_append_value (&layers, &value);
      g_value_set_string (&value, "/model.22/Mul_2");
=======
    case GST_YOLO_TYPE_V8:
      g_object_set (G_OBJECT (qtimlelement), "model", model_path, NULL);
      g_value_set_string (&value, "Mul_248");
      gst_value_array_append_value (&layers, &value);
      g_value_set_string (&value, "Sigmoid_249");
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
      gst_value_array_append_value (&layers, &value);
      g_object_set_property (G_OBJECT (qtimlelement), "layers", &layers);

      // Get enum value of module property from qtimlvdetection plugin
<<<<<<< HEAD
      module_enum = get_enum_value (qtimlvdetection, "module" , "yolov8");
      if (module_enum != -1) {
        g_object_set (G_OBJECT (qtimlvdetection), "module", module_enum, NULL);
      } else {
        g_printerr ("Module yolov5s is not available in qtimlvdetection\n");
=======
      module_id = get_enum_value (qtimlvdetection, "module", "yolov8");
      if (module_id != -1) {
        g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
      } else {
        g_printerr ("Module yolov8 is not available in qtimlvdetection\n");
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
        goto error;
      }
      g_object_set (G_OBJECT (qtimlvdetection), "labels", labels_path, NULL);

      // Set qtimlvdetection properties
      g_object_set (G_OBJECT (qtimlvdetection), "threshold", 50.0, NULL);
      g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
      break;

    // Set YoloNas specific settings
<<<<<<< HEAD
    case YOLO_TYPE_NAS:
=======
    case GST_YOLO_TYPE_NAS:
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
      g_object_set (G_OBJECT (qtimlelement), "model", model_path, NULL);
      g_value_set_string (&value, "/heads/Mul");
      gst_value_array_append_value (&layers, &value);
      g_value_set_string (&value, "/heads/Sigmoid");
      gst_value_array_append_value (&layers, &value);
      g_object_set_property (G_OBJECT (qtimlelement), "layers", &layers);

      // Get enum value of module property from qtimlvdetection plugin
<<<<<<< HEAD
      module_enum = get_enum_value (qtimlvdetection, "module" , "yolo-nas");
      if (module_enum != -1) {
        g_object_set (G_OBJECT (qtimlvdetection), "module", module_enum, NULL);
=======
      module_id = get_enum_value (qtimlvdetection, "module", "yolo-nas");
      if (module_id != -1) {
        g_object_set (G_OBJECT (qtimlvdetection), "module", module_id, NULL);
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
      } else {
        g_printerr ("Module yolo-nas is not available in qtimlvdetection\n");
        goto error;
      }
      g_object_set (G_OBJECT (qtimlvdetection), "labels", labels_path, NULL);

      // Set qtimlvdetection properties
      g_object_set (G_OBJECT (qtimlvdetection), "threshold", 51.0, NULL);
      g_object_set (G_OBJECT (qtimlvdetection), "results", 10, NULL);
      break;
    default:
      g_printerr ("Invalid Yolo Model type");
      goto error;
  }

  // 2.4 Set the properties of Wayland compositor
  g_object_set (G_OBJECT (waylandsink), "sync", FALSE, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", true, NULL);

<<<<<<< HEAD
  // Setting the properties of pad_filter for negotiation with qtivcomposer
=======
  // 2.5 Set the properties of fpsdisplaysink plugin- sync,
  // signal-fps-measurements, text-overlay and video-sink
  g_object_set (G_OBJECT (fpsdisplaysink), "signal-fps-measurements", true, NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "text-overlay", true, NULL);
  g_object_set (G_OBJECT (fpsdisplaysink), "video-sink", waylandsink, NULL);

  // Set the properties of pad_filter for negotiation with qtivcomposer
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
  pad_filter = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "BGRA",
      "width", G_TYPE_INT, 640,
      "height", G_TYPE_INT, 360, NULL);

  g_object_set (G_OBJECT (detection_filter), "caps", pad_filter, NULL);
  gst_caps_unref (pad_filter);

  // 3. Setup the pipeline
<<<<<<< HEAD

  g_print ("Adding all elements to the pipeline...\n");

  gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, main_capsfilter,
      tee, qtimlvconverter, qtimlelement, qtimlvdetection,
      detection_filter, qtivcomposer, waylandsink, NULL);

  for (int i = 0; i < QUEUE_COUNT; i++) {
=======
  g_print ("Adding all elements to the pipeline...\n");

  gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, qmmfsrc_caps,
      qtivtransform, tee, qtimlvconverter, qtimlelement, qtimlvdetection,
      detection_filter, qtivcomposer, fpsdisplaysink, NULL);

  for (gint i = 0; i < QUEUE_COUNT; i++) {
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  g_print ("Linking elements...\n");

<<<<<<< HEAD
  //  Creating Pipeline for Classification:
  //  qtiqmmfsrc (Camera) -> main_capsfilter -> tee (SPLIT)
  //      | tee -> qtivcomposer
  //      |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
  //      qtivcomposer (COMPOSITION) -> waylandsink (Display)
  //      Pre process: qtimlvconverter
  //      ML Framework: qtimlsnpe/qtimltflite
  //      Post process: qtimlvdetection -> detection_filter
  ret = gst_element_link_many (qtiqmmfsrc, main_capsfilter, queue[0], tee, NULL);
=======
  // Create Pipeline for Object Detection
  ret = gst_element_link_many (qtiqmmfsrc, qmmfsrc_caps,
      qtivtransform, queue[0], tee, NULL);
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for qmmfsource->tee\n");
    goto error;
  }

<<<<<<< HEAD
  ret = gst_element_link_many (qtivcomposer, queue[1], waylandsink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for"
        "qtivcomposer->waylandsink.\n");
=======
  ret = gst_element_link_many (qtivcomposer, queue[1], fpsdisplaysink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for"
        "qtivcomposer->fpsdisplaysink\n");
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
    goto error;
  }

  ret = gst_element_link_many (tee, queue[2], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for tee->qtivcomposer.\n");
    goto error;
  }

  ret = gst_element_link_many (
      tee, queue[3], qtimlvconverter, queue[4],
      qtimlelement, queue[5], qtimlvdetection,
      detection_filter, queue[6], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for"
        "pre proc -> ml framework -> post proc.\n");
    goto error;
  }

  return TRUE;

<<<<<<< HEAD
  // For any errors in plugin creation, cleanup earlier created ones
error:
  gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, main_capsfilter,
      tee, qtivcomposer, qtimlvconverter, qtimlelement, qtimlvdetection,
      detection_filter, waylandsink, NULL);

  for (int i = 0; i < QUEUE_COUNT; i++) {
=======
error:
  gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, qmmfsrc_caps,
      qtivtransform, tee, qtimlvconverter, qtimlelement, qtimlvdetection,
      detection_filter, qtivcomposer, fpsdisplaysink, NULL);
  for (gint i = 0; i < QUEUE_COUNT; i++) {
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
    gst_bin_remove_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  return FALSE;
}

/**
 * Unlinks and removes all elements.
 *
 * @param appctx Application Context Pointer.
 */
static void
destroy_pipe (GstAppContext * appctx)
{
  GstElement *curr = (GstElement *) appctx->plugins->data;
  GstElement *next;
<<<<<<< HEAD

  GList *list = appctx->plugins->next;
=======
  GList *list = appctx->plugins->next;

>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
  for ( ; list != NULL; list = list->next) {
    next = (GstElement *) list->data;
    gst_element_unlink (curr, next);
    gst_bin_remove (GST_BIN (appctx->pipeline), curr);
    curr = next;
  }
  gst_bin_remove (GST_BIN (appctx->pipeline), curr);

  g_list_free (appctx->plugins);
  appctx->plugins = NULL;
  gst_object_unref (appctx->pipeline);
}

<<<<<<< HEAD
/**
 * Main Function Of the Application.
 */
gint
main (gint argc, gchar * argv[])
{
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
=======
gint
main (gint argc, gchar * argv[])
{
  GstBus *bus = NULL;
  GMainLoop *mloop = NULL;
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
  GstElement *pipeline = NULL;
  GOptionContext *ctx = NULL;
  const gchar *model_path = NULL;
  const gchar *labels_path = NULL;
<<<<<<< HEAD
  const char *app_name = strrchr (argv[0], '/') ?
      (strrchr (argv[0], '/') + 1) : argv[0];
  GstAppContext appctx = {};
  YoloModelType model_type = YOLO_TYPE_NONE;
  guint intrpt_watch_id = 0;
  gboolean yolov5 = FALSE;
  gboolean yolov8 = FALSE;
  gboolean yolonas = FALSE;
  gboolean ret = FALSE;
  gchar help_description[1024];

  // Structure to define the user options selection
  GOptionEntry entries[] = {
    { "yolov5", 0, 0, G_OPTION_ARG_NONE,
      &yolov5,
      "Execute yolov5 Model",
      NULL
    },
    { "yolov8", 0, 0, G_OPTION_ARG_NONE,
      &yolov8,
      "Execute yolov8 Model",
      NULL
    },
    { "yolonas", 0, 0, G_OPTION_ARG_NONE,
      &yolonas,
      "Execute yolonas Model",
      NULL
=======
  const gchar *app_name = NULL;
  GstAppContext appctx = {};
  GstYoloModelType model_type = GST_YOLO_TYPE_V5;
  gboolean ret = FALSE;
  gchar help_description[1024];
  guint intrpt_watch_id = 0;

  // Set Display environment variables
  setenv("XDG_RUNTIME_DIR", "/run/user/root", 0);
  setenv("WAYLAND_DISPLAY", "wayland-1", 0);

  // Structure to define the user options selection
  GOptionEntry entries[] = {
    { "model-type", 't', 0, G_OPTION_ARG_INT,
      &model_type,
      "Yolo Model version to Execute: Yolov5 (1), Yolov8 (2), YoloNas (3)",
      "1 or 2 or 3"
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
    },
    { "model", 'm', 0, G_OPTION_ARG_STRING,
      &model_path,
      "This is an optional parameter and overrides default path\n"
      "      Default model path for YOLOV5 DLC: " DEFAULT_SNPE_YOLOV5_MODEL "\n"
      "      Default model path for YOLOV8 DLC: " DEFAULT_SNPE_YOLOV8_MODEL "\n"
      "      Default model path for YOLO NAS DLC: " DEFAULT_SNPE_YOLONAS_MODEL,
      "/PATH"
    },
    { "labels", 'l', 0, G_OPTION_ARG_STRING,
      &labels_path,
      "This is an optional parameter and overrides default path\n"
      "      Default labels path for YOLOV5: " DEFAULT_YOLOV5_LABELS "\n"
      "      Default labels path for YOLOV8: " DEFAULT_YOLOV8_LABELS "\n"
      "      Default labels path for YOLO NAS: " DEFAULT_YOLONAS_LABELS,
      "/PATH"
    },
    { NULL }
  };

<<<<<<< HEAD
  snprintf (help_description, 1023, "\nExample:\n"
      "  %s --yolov5\n"
      "  %s --yolonas --model=%s --labels=%s\n"
      "\nThis Sample App demonstrates Object Detection on Live Stream",
      app_name, app_name, DEFAULT_SNPE_YOLONAS_MODEL, DEFAULT_YOLONAS_LABELS);

  // Parse command line entries.
  if ((ctx = g_option_context_new (help_description)) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;
=======
  app_name = strrchr (argv[0], '/') ? (strrchr (argv[0], '/') + 1) : argv[0];

  snprintf (help_description, 1023, "\nExample:\n"
      "  %s --model-type=1\n"
      "  %s -t 3 --model=%s --labels=%s\n"
      "\nThis Sample App demonstrates Object Detection on Live Stream",
      app_name, app_name, DEFAULT_SNPE_YOLONAS_MODEL, DEFAULT_YOLONAS_LABELS);
  help_description[1023] = '\0';

  // Parse command line entries.
  if ((ctx = g_option_context_new (help_description)) != NULL) {
    GError *error = NULL;
    gboolean success = FALSE;
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("Failed to parse command line options: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -EFAULT;
    } else if (!success && (NULL == error)) {
      g_printerr ("Initializing: Unknown error!\n");
      return -EFAULT;
    }
  } else {
    g_printerr ("Failed to create options context!\n");
    return -EFAULT;
  }

<<<<<<< HEAD
  // No Yolo Model selected, default to Yolov5
  if ((yolov5 == FALSE) && (yolov8 == FALSE) &&  (yolonas == FALSE)) {
    g_print ("Using Yolov5 as default Model.\n");
    yolov5 = TRUE;
  }

  if ((yolov5 + yolov8 + yolonas) > TRUE) {
    g_printerr ("Cannot combine multiple model options in single command\n");
    return -EINVAL;
  }

  // Selecting model type based on user selection
  model_type = yolov5 ? YOLO_TYPE_V5 :
      (yolov8 ? YOLO_TYPE_V8 : YOLO_TYPE_NAS);

  // Setting default model path for execution
  model_path = model_path ? model_path:
      (yolov5 ? DEFAULT_SNPE_YOLOV5_MODEL :
      (yolov8 ? DEFAULT_SNPE_YOLOV8_MODEL : DEFAULT_SNPE_YOLONAS_MODEL));

  // Setting default Label path for execution
  labels_path = labels_path ? labels_path:
      (yolov5 ? DEFAULT_YOLOV5_LABELS :
      (yolov8 ? DEFAULT_YOLOV8_LABELS : DEFAULT_YOLONAS_LABELS));
=======
  if (model_type < GST_YOLO_TYPE_V5 ||
      model_type > GST_YOLO_TYPE_NAS) {
    g_printerr ("Invalid model-version option selected\n"
        "Available options:\n"
        "    Yolov5: %d\n"
        "    Yolov8: %d\n"
        "    YoloNas: %d\n",
        GST_YOLO_TYPE_V5, GST_YOLO_TYPE_V8, GST_YOLO_TYPE_NAS);
    return -EINVAL;
  }

  // Set model path for execution
  model_path = model_path ? model_path:
      (model_type == GST_YOLO_TYPE_V5 ? DEFAULT_SNPE_YOLOV5_MODEL :
      (model_type == GST_YOLO_TYPE_V8 ? DEFAULT_SNPE_YOLOV8_MODEL :
      DEFAULT_SNPE_YOLONAS_MODEL));

  // Set default Label path for execution
  labels_path = labels_path ? labels_path:
      (model_type == GST_YOLO_TYPE_V5 ? DEFAULT_YOLOV5_LABELS :
      (model_type == GST_YOLO_TYPE_V8 ? DEFAULT_YOLOV8_LABELS :
      DEFAULT_YOLONAS_LABELS));
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024

  g_print ("Running app with model: %s and labels: %s\n",
      model_path, labels_path);

<<<<<<< HEAD
  if (model_path == NULL || labels_path == NULL) {
    g_printerr ("Model or Labels cannot be null\n");
    return -EINVAL;
  }

  // Initialize GST library.
  argc = 1;
=======
  // Initialize GST library.
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
  gst_init (&argc, &argv);

  // Create the pipeline that will form connection with other elements
  pipeline = gst_pipeline_new (app_name);
  if (!pipeline) {
    g_printerr ("ERROR: failed to create pipeline.\n");
    return -1;
  }

  appctx.pipeline = pipeline;

  // Build the pipeline, link all elements in the pipeline
  ret = create_pipe (&appctx, model_type, model_path, labels_path);
  if (!ret) {
    g_printerr ("ERROR: failed to create GST pipe.\n");
    destroy_pipe (&appctx);
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    destroy_pipe (&appctx);
    return -1;
  }
  appctx.mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  // Bus is message queue for getting callback from gstreamer pipeline
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    g_main_loop_unref (mloop);
    destroy_pipe (&appctx);
    return -1;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);

<<<<<<< HEAD
  // Call respective callback function based on message
=======
  // Register respective callback function based on message
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), pipeline);

  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);

  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, &appctx);

  // On successful transition to PAUSED state, state_changed_cb is called.
  // state_changed_cb callback is used to send pipeline to play state.
<<<<<<< HEAD
  g_print ("Setting pipeline to PAUSED state ...\n");
=======
  g_print ("Set pipeline to PAUSED state ...\n");
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
  switch (gst_element_set_state (pipeline, GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PAUSED state!\n");
      goto error;
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

  // Wait till pipeline encounters an error or EOS
  g_print ("g_main_loop_run\n");
  g_main_loop_run (mloop);
  g_print ("g_main_loop_run ends\n");

error:
  g_source_remove (intrpt_watch_id);
  g_main_loop_unref (mloop);

<<<<<<< HEAD
  g_print ("Setting pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Destory pipeline\n");
=======
  g_print ("Set pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Destroy pipeline\n");
>>>>>>> 35f72b4763d1db34730f71b2dac50b2b4c024024
  destroy_pipe (&appctx);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
