/**
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/**
 * Application:
 * AI based Parallel Classification, Pose Detection, Object Detection and
 * Segmentation on Live stream (4-Streams).
 *
 * Description:
 * The application takes live video stream from camera and gives same to
 * 4 Ch parallel processing by AI models (Classification, Pose detection and
 * Object detection and Segmentation) and display scaled down preview with
 * overlayed AI models output composed as 2x2 matrix
 *
 * Pipeline for Gstreamer Parallel Inferencing (4 Stream) below:
 * qtiqmmfsrc (Camera) -> main_capsfilter -> tee (SPLIT)
 *     | tee -> qtivcomposer
 *     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *     | tee -> qtivcomposer
 *     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *     | tee -> qtivcomposer
 *     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *     | tee -> qtivcomposer
 *     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
 *     qtivcomposer (COMPOSITION) -> waylandsink (Display)
 *     Pre process: qtimlvconverter
 *     ML Framework: qtimlsnpe/qtimltflite
 *     Post process: qtimlvdetection/qtimlclassification/
 *                   qtimlvsegmentation/qtimlvpose -> detection_filter
 */

#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>

#include "include/gst_sample_apps_utils.h"

/**
 * Default models path and labels path
 */
#define DEFAULT_SNPE_OBJECT_DETECTION_MODEL "/opt/yolov5.dlc"
#define DEFAULT_OBJECT_DETECTION_LABELS "/opt/yolov5.labels"
#define DEFAULT_TFLITE_CLASSIFICATION_MODEL "/opt/resnet50.tflite"
#define DEFAULT_CLASSIFICATION_LABELS "/opt/resnet50.labels"
#define DEFAULT_TFLITE_POSE_DETECTION_MODEL "/opt/posenet_mobilenet_v1.tflite"
#define DEFAULT_POSE_DETECTION_LABELS "/opt/posenet_mobilenet_v1.labels"
#define DEFAULT_SNPE_SEGMENTATION_MODEL "/opt/deeplabv3_resnet50.dlc"
#define DEFAULT_SEGMENTATION_LABELS "/opt/deeplabv3_resnet50.labels"

/**
 * Default setting of camera output resolution, Scaling of camera output
 * will be done in qtimlvconverter based on model input size
 */
#define DEFAULT_CAMERA_OUTPUT_WIDTH 1280
#define DEFAULT_CAMERA_OUTPUT_HEIGHT 720
#define DEFAULT_CAMERA_FRAME_RATE 30

/**
 * Number of Queues used for buffer caching between elements
 */
#define QUEUE_COUNT 22

/**
 * InferenceType:
 * @param OBJECT_DETECTION: Object detection Pipeline.
 * @param CLASSIFICATION: Classification Pipeline.
 * @param POSE_DETECTION: Pose detection Pipeline.
 * @param SEGMENTATION: Segmentation Pipeline.
 * @param PIPELINE_CNT
 *
 * Type of Inference.
 */
typedef enum {
  OBJECT_DETECTION,
  CLASSIFICATION,
  POSE_DETECTION,
  SEGMENTATION,
  PIPELINE_CNT
} InferenceType;

/**
 * WindowRect:
 * @param x: X-Cordinate
 * @param y: Y-Cordinate
 * @param width: Window Width
 * @param height: Window Height
 *
 * Window Dimension and Co-ordinate.
 */
typedef struct {
  int x, y;
  int width, height;
} WindowRect;

/**
 * PipelineData:
 * @model: Path to model file
 * @labels: Path to label file
 * @mlframework: ML inference plugin
 * @postproc: Post processing plugin
 * @delegate: ML Execution Core.
 * @position: Window Dimension and Co-ordinate.
 *
 * Pipeline Data context to use plugins and path of Model, Labels.
 */
typedef struct {
  const char * model;
  const char * labels;
  const char * preproc;
  const char * mlframework;
  const char * postproc;
  int delegate;
  WindowRect position;
} PipelineData;

/**
 * Default properties of pipeline for Object detection,
 * Classification, Postdetection and Segmentation
 */
static PipelineData pipeline_data[PIPELINE_CNT] = {
  {DEFAULT_SNPE_OBJECT_DETECTION_MODEL, DEFAULT_OBJECT_DETECTION_LABELS,
      "qtimlvconverter", "qtimlsnpe", "qtimlvdetection",
      GST_ML_SNPE_DELEGATE_DSP, {0, 0, 960, 540}},
  {DEFAULT_TFLITE_CLASSIFICATION_MODEL, DEFAULT_CLASSIFICATION_LABELS,
      "qtimlvconverter", "qtimltflite", "qtimlvclassification",
      GST_ML_TFLITE_DELEGATE_EXTERNAL, {960, 0, 960, 540}},
  {DEFAULT_TFLITE_POSE_DETECTION_MODEL, DEFAULT_POSE_DETECTION_LABELS,
      "qtimlvconverter", "qtimltflite", "qtimlvpose",
      GST_ML_TFLITE_DELEGATE_EXTERNAL, {0, 540, 960, 540}},
  {DEFAULT_SNPE_SEGMENTATION_MODEL, DEFAULT_SEGMENTATION_LABELS,
      "qtimlvconverter", "qtimlsnpe", "qtimlvsegmentation",
      GST_ML_SNPE_DELEGATE_DSP, {960, 540, 960, 540}}
};

/**
 * Build Property for pad.
 *
 * @param property Property Name.
 * @param values Value of Property.
 * @param num count of Property Values.
 */
static void
build_pad_property (GValue * property, gint values[], int num)
{
  GValue val = G_VALUE_INIT;
  g_value_init (&val, G_TYPE_INT);

  for (int idx = 0; idx < num; idx++) {
    g_value_set_int (&val, values[idx]);
    gst_value_array_append_value (property, &val);
  }

  g_value_unset (&val);
}

/**
 * Help Menu of the Application.
 *
 * @param app_name Application executable name.
 */
void
help (const gchar *app_name)
{
  g_print ("Usage: %s \n", app_name);
  g_print ("\nThis Sample App demonstrates Classification, Segmemtation\n");
  g_print ("Object Detection, Pose Detection On Live Stream ");
  g_print ("and output 4 Parallel Stream.\n\n");
  g_print ("Default Path for model and labels used are as below:\n");
  g_print ("  ------------------------------------------------------------"
      "--------------------------\n");
  g_print ("  |Algorithm         %-32s  %-32s|\n", "Model", "Labels");
  g_print ("  ------------------------------------------------------------"
      "--------------------------\n");
  g_print ("  |Object detection  %-32s  %-32s|\n",
      DEFAULT_SNPE_OBJECT_DETECTION_MODEL, DEFAULT_OBJECT_DETECTION_LABELS);
  g_print ("  |Pose estimation   %-32s  %-32s|\n",
      DEFAULT_TFLITE_POSE_DETECTION_MODEL, DEFAULT_POSE_DETECTION_LABELS);
  g_print ("  |Segmentation      %-32s  %-32s|\n",
      DEFAULT_SNPE_SEGMENTATION_MODEL, DEFAULT_SEGMENTATION_LABELS);
  g_print ("  |Classification    %-32s  %-32s|\n",
      DEFAULT_TFLITE_CLASSIFICATION_MODEL, DEFAULT_CLASSIFICATION_LABELS);
  g_print ("  ------------------------------------------------------------"
      "--------------------------\n");

  g_print ("\nTo use your own model and labels replace at the default paths\n");
}

/**
 * Create GST pipeline: has 3 main steps
 * 1. Create all elements/GST Plugins
 * 2. Set Paramters for each plugin
 * 3. Link plugins to create GST pipeline
 *
 * @param appctx Application Context Pointer.
 */
static gboolean
create_pipe (GstAppContext * appctx)
{
  GstElement *qtiqmmfsrc, *main_capsfilter, *tee;
  GstElement *qtimlvconverter[PIPELINE_CNT], *qtimlelement[PIPELINE_CNT];
  GstElement *qtimlvpostproc[PIPELINE_CNT], *detection_filter[PIPELINE_CNT];
  GstElement *qtivcomposer, *waylandsink, *queue[QUEUE_COUNT];
  GstPad *composer_sink[PIPELINE_CNT*2];
  GstCaps *filtercaps;
  GstStructure *delegate_options;
  GValue position[PIPELINE_CNT], dimension[PIPELINE_CNT];
  gint pos_vals[PIPELINE_CNT][2], dim_vals[PIPELINE_CNT][2];
  gdouble alpha_value;
  gint module_enum;
  gchar element_name[128];
  gboolean ret = FALSE;
  gint width = DEFAULT_CAMERA_OUTPUT_WIDTH;
  gint height = DEFAULT_CAMERA_OUTPUT_HEIGHT;
  gint framerate = DEFAULT_CAMERA_FRAME_RATE;

  // 1. Create the elements or Plugins

  // get live camera stream using qtiqmmfsrc plugin
  qtiqmmfsrc = gst_element_factory_make ("qtiqmmfsrc", "qtiqmmfsrc");
  if (!qtiqmmfsrc) {
    g_printerr ("Failed to create qtiqmmfsrc\n");
    return FALSE;
  }

  // Use capsfilter to define the camera output settings
  main_capsfilter = gst_element_factory_make ("capsfilter", "main_capsfilter");
  if (!main_capsfilter) {
    g_printerr ("Failed to create main_capsfilter\n");
    return FALSE;
  }

  // Use tee to send same data buffer
  // one for AI inferencing, one for Display composition
  tee = gst_element_factory_make ("tee", "tee");
  if (!tee) {
    g_printerr ("Failed to create tee\n");
    return FALSE;
  }

  // Create 4 pipelines for AI inferencing on same camera stream
  for (int i = 0; i < PIPELINE_CNT; i++) {
    const char *preproc = pipeline_data[i].preproc;
    const char *mlframework = pipeline_data[i].mlframework;
    const char *postproc = pipeline_data[i].postproc;

    // Create qtimlvconverter for Input preprocessing
    snprintf (element_name, 127, "%s-%d", preproc, i);
    qtimlvconverter[i] = gst_element_factory_make (preproc, element_name);
    if (!qtimlvconverter[i]) {
      g_printerr ("Failed to create qtimlvconverter %d\n", i);
      return FALSE;
    }

    // Creating the ML inferencing plugin SNPE/TFLite
    snprintf (element_name, 127, "%s-%d", mlframework, i);
    qtimlelement[i] = gst_element_factory_make (mlframework, element_name);
    if (!qtimlelement[i]) {
      g_printerr ("Failed to create qtimlelement %d\n", i);
      return FALSE;
    }

    // Plugin for ML postprocessing based on config
    snprintf (element_name, 127, "%s-%d", postproc, i);
    qtimlvpostproc[i] = gst_element_factory_make (postproc, element_name);
    if (!qtimlvpostproc[i]) {
      g_printerr ("Failed to create qtimlvpostproc %d\n", i);
      return FALSE;
    }

    // Capsfilter to get matching parms of ML post proc o/p and qtivcomposer
    snprintf (element_name, 127, "capsfilter-%d", i);
    detection_filter[i] = gst_element_factory_make ("capsfilter", element_name);
    if (!detection_filter[i]) {
      g_printerr ("Failed to create detection_filter %d\n", i);
      return FALSE;
    }
  }

  // Creating queue to decouple the processing on sink and source pad.
  for (int i = 0; i < QUEUE_COUNT; i++) {
    snprintf (element_name, 127, "queue-%d", i);
    queue[i] = gst_element_factory_make ("queue", element_name);
    if (!queue[i]) {
      g_printerr ("Failed to create queue %d\n", i);
      return FALSE;
    }
  }

  // Composer to combine 4 input streams as 2x2 matrix in single display
  qtivcomposer = gst_element_factory_make ("qtivcomposer", "qtivcomposer");
  if (!qtivcomposer) {
    g_printerr ("Failed to create qtivcomposer\n");
    return FALSE;
  }

  // Creating Wayland compositor to render output on Display
  waylandsink = gst_element_factory_make ("waylandsink", "waylandsink");
  if (!waylandsink) {
    g_printerr ("Failed to create waylandsink \n");
    return FALSE;
  }

  // 1.1 Append all elements in a list for cleanup
  appctx->plugins = NULL;
  appctx->plugins = g_list_append (appctx->plugins, qtiqmmfsrc);
  appctx->plugins = g_list_append (appctx->plugins, tee);
  appctx->plugins = g_list_append (appctx->plugins, main_capsfilter);

  for (int i = 0; i < PIPELINE_CNT; i++) {
    appctx->plugins = g_list_append (appctx->plugins, qtimlvconverter[i]);
    appctx->plugins = g_list_append (appctx->plugins, qtimlelement[i]);
    appctx->plugins = g_list_append (appctx->plugins, qtimlvpostproc[i]);
    appctx->plugins = g_list_append (appctx->plugins, detection_filter[i]);
  }

  for (int i = 0; i < QUEUE_COUNT; i++) {
    appctx->plugins = g_list_append (appctx->plugins, queue[i]);
  }

  appctx->plugins = g_list_append (appctx->plugins, qtivcomposer);
  appctx->plugins = g_list_append (appctx->plugins, waylandsink);

  // 2. Set properties for all GST plugin elements

  // 2.1 set properties for 4 AI pipelines, like HW, Model, Post proc
  for (int i = 0; i < PIPELINE_CNT; i++) {
    GValue layers = G_VALUE_INIT;
    GValue value = G_VALUE_INIT;

    // Set ML plugin properties: HW, Model, Labels
    g_object_set (G_OBJECT (qtimlelement[i]),
        "delegate", pipeline_data[i].delegate, NULL);
    if (pipeline_data[i].delegate == GST_ML_TFLITE_DELEGATE_EXTERNAL) {
      delegate_options = gst_structure_from_string (
          "QNNExternalDelegate,backend_type=htp;", NULL);
      g_object_set (G_OBJECT (qtimlelement[i]),
          "external-delegate-path", "libQnnTFLiteDelegate.so", NULL);
      g_object_set (G_OBJECT (qtimlelement[i]),
          "external-delegate-options", delegate_options, NULL);
      gst_structure_free (delegate_options);
    }

    g_object_set (G_OBJECT (qtimlelement[i]),
        "model", pipeline_data[i].model, NULL);
    g_object_set (G_OBJECT (qtimlvpostproc[i]),
        "labels", pipeline_data[i].labels, NULL);

    // Set properties for ML postproc plugins- module, layers, threshold
    switch (i) {
      case OBJECT_DETECTION:
        g_value_init (&layers, GST_TYPE_ARRAY);
        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, "Conv_198");
        gst_value_array_append_value (&layers, &value);
        g_value_set_string (&value, "Conv_232");
        gst_value_array_append_value (&layers, &value);
        g_value_set_string (&value, "Conv_266");
        gst_value_array_append_value (&layers, &value);
        g_object_set_property (G_OBJECT (qtimlelement[i]), "layers", &layers);
        module_enum = get_enum_value (qtimlvpostproc[i], "module" , "yolov5");
        if (module_enum!=-1) {
          g_object_set (G_OBJECT (qtimlvpostproc[i]),
              "module", module_enum, NULL);
        } else {
          g_printerr ("Module yolov5 is not available in qtimlvdetection\n");
          goto error;
        }
        // Setting the object detection module threshold limit
        g_object_set (G_OBJECT (qtimlvpostproc[OBJECT_DETECTION]),
            "threshold", 40.0, "results", 10, NULL);
        break;

      case CLASSIFICATION:
        module_enum = get_enum_value (qtimlvpostproc[i], "module" , "mobilenet");
        if (module_enum != -1) {
          g_object_set (G_OBJECT (qtimlvpostproc[i]), "threshold", 20.0,
              "results", 3, "module", module_enum, NULL);
        } else {
          g_printerr ("Module mobilenet not available in qtimlvclassifivation\n");
          goto error;
        }
        break;

      case POSE_DETECTION:
        module_enum = get_enum_value (qtimlvpostproc[i], "module" , "posenet");
        if (module_enum != -1) {
          g_object_set (G_OBJECT (qtimlvpostproc[i]), "threshold", 51.0,
              "results", 2, "module", module_enum, "constants",
              "Posenet,q-offsets=<128.0,128.0,117.0>,q-scales="
              "<0.0784313753247261,0.0784313753247261,1.3875764608383179>;",
              NULL);
        } else {
          g_printerr ("Module posenet is not available in qtimlvpose\n");
          goto error;
        }
        break;

      case SEGMENTATION:
        module_enum = get_enum_value (qtimlvpostproc[i], "module" ,
            "deeplab-argmax");
        if (module_enum != -1) {
          g_object_set (G_OBJECT (qtimlvpostproc[i]),
              "module", module_enum, NULL);
        } else {
          g_printerr ("Module deeplab-argmax is not available in "
              "qtimlvsegmentation\n");
          goto error;
        }
        break;
      default:
        g_printerr ("Cannot be here");
        goto error;
    }
  }

  // 2.2 Set the capabilities of camera plugin output
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, framerate, 1,
      "compression", G_TYPE_STRING, "ubwc", NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));

  g_object_set (G_OBJECT (main_capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);


  // Setting the properties of pad_filter for negotiation with qtivcomposer
  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "BGRA",
      "width", G_TYPE_INT, 640,
      "height", G_TYPE_INT, 360, NULL);

  for (int i = 0; i < PIPELINE_CNT-1; i++)
    g_object_set (G_OBJECT (detection_filter[i]), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  filtercaps = gst_caps_new_simple ("video/x-raw", "width", G_TYPE_INT, 256,
      "height", G_TYPE_INT, 144, NULL);

  g_object_set (G_OBJECT (detection_filter[SEGMENTATION]),
      "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  // 2.3 Setting the properties of Wayland compositor
  g_object_set (G_OBJECT (waylandsink), "sync", FALSE, NULL);
  g_object_set (G_OBJECT (waylandsink), "fullscreen", true, NULL);

  // 3. Setup the pipeline

  g_print ("Adding all elements to the pipeline...\n");

  gst_bin_add_many (GST_BIN (appctx->pipeline), qtiqmmfsrc,  main_capsfilter,
      tee, qtivcomposer, waylandsink, NULL);

  for (int i = 0; i < PIPELINE_CNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), qtimlvconverter[i],
        qtimlelement[i], qtimlvpostproc[i], detection_filter[i], NULL);
  }

  for (int i = 0; i < QUEUE_COUNT; i++) {
    gst_bin_add_many (GST_BIN (appctx->pipeline), queue[i], NULL);
  }

  g_print ("Linking elements...\n");

  // Creating pipeline for Parallel Inferencing
  // qtiqmmfsrc (Camera) -> main_capsfilter -> tee (SPLIT)
  //     | tee -> qtivcomposer
  //     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
  //     | tee -> qtivcomposer
  //     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
  //     | tee -> qtivcomposer
  //     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
  //     | tee -> qtivcomposer
  //     |     -> Pre process-> ML Framework -> Post process -> qtivcomposer
  //     qtivcomposer (COMPOSITION) -> waylandsink (Display)
  //     Pre process: qtimlvconverter
  //     ML Framework: qtimlsnpe/qtimltflite
  //     Post process: qtimlvdetection/qtimlclassification/
  //                   qtimlvsegmentation/qtimlvpose -> detection_filter
  ret = gst_element_link_many (qtiqmmfsrc, main_capsfilter, queue[0], tee, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for qmmfsource->tee\n");
    goto error;
  }

  // 3.1 Creating links for stream 1 - OBJECT_DETECTION
  ret = gst_element_link_many (
      tee, queue[1], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for object detection 1.\n");
    goto error;
  }

  ret = gst_element_link_many (
      tee, queue[2], qtimlvconverter[OBJECT_DETECTION], queue[3],
      qtimlelement[OBJECT_DETECTION], queue[4], qtimlvpostproc[OBJECT_DETECTION],
      detection_filter[OBJECT_DETECTION], queue[5], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for object detection 2.\n");
    goto error;
  }

  // 3.2 Creating links for stream 2 - CLASSIFICATION
  ret = gst_element_link_many (
      tee, queue[6], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for classification 1.\n");
    goto error;
  }

  ret = gst_element_link_many (
      tee, queue[7], qtimlvconverter[CLASSIFICATION], queue[8],
      qtimlelement[CLASSIFICATION], queue[9], qtimlvpostproc[CLASSIFICATION],
      detection_filter[CLASSIFICATION], queue[10], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for classification 2.\n");
    goto error;
  }

  // 3.3 Creating links for stream 3 - POSE DETECTION
  ret = gst_element_link_many (
      tee, queue[11], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for pose detection 1.\n");
    goto error;
  }

  ret = gst_element_link_many (
      tee, queue[12], qtimlvconverter[POSE_DETECTION], queue[13],
      qtimlelement[POSE_DETECTION], queue[14], qtimlvpostproc[POSE_DETECTION],
      detection_filter[POSE_DETECTION], queue[15], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for pose detection 2.\n");
    goto error;
  }

  // 3.4 Creating links for stream 4 - SEGMENTATION
  ret = gst_element_link_many (
      tee, queue[16] , qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for segmentation 1.\n");
    goto error;
  }

  ret = gst_element_link_many (
      tee, queue[17], qtimlvconverter[SEGMENTATION], queue[18],
      qtimlelement[SEGMENTATION], queue[19], qtimlvpostproc[SEGMENTATION],
      detection_filter[SEGMENTATION], queue[20], qtivcomposer, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for segmentation 2.\n");
    goto error;
  }

  ret = gst_element_link_many (qtivcomposer, queue[21], waylandsink, NULL);
  if (!ret) {
    g_printerr ("Pipeline elements cannot be linked for composer->wayland.\n");
    goto error;
  }

  g_print ("All elements are linked successfully\n");

  for (int i = 0; i < PIPELINE_CNT*2; i++) {
    snprintf (element_name, 127, "sink_%d", i);
    composer_sink[i] = gst_element_get_static_pad (qtivcomposer, element_name);
    if (!composer_sink[i]) {
      g_printerr ("One or more sink pads are not ref'ed  composer_sink %d\n", i);
      goto error;
    }
  }

  // 3.5 Set position of all inference windows in a grid pattern
  for (int i = 0; i < PIPELINE_CNT; i++) {
    position[i] = G_VALUE_INIT;
    dimension[i] = G_VALUE_INIT;
    g_value_init (&position[i], GST_TYPE_ARRAY);
    g_value_init (&dimension[i], GST_TYPE_ARRAY);

    WindowRect pos = pipeline_data[i].position;
    pos_vals[i][0] = pos.x; pos_vals[i][1] = pos.y;
    dim_vals[i][0] = pos.width; dim_vals[i][1] = pos.height;
    build_pad_property (&position[i], pos_vals[i], 2);
    build_pad_property (&dimension[i], dim_vals[i], 2);

    g_object_set_property (G_OBJECT (composer_sink[i*2]),
        "position", &position[i]);
    g_object_set_property (G_OBJECT (composer_sink[i*2]),
        "dimensions", &dimension[i]);
    g_object_set_property (G_OBJECT (composer_sink[i*2 + 1]),
        "position", &position[i]);
    g_object_set_property (G_OBJECT (composer_sink[i*2 + 1]),
        "dimensions", &dimension[i]);
  }

  // Setting overlay window size for Classification to display text labels
  dimension[0] = G_VALUE_INIT;
  g_value_init (&dimension[0], GST_TYPE_ARRAY);

  dim_vals[0][0] = 320;
  dim_vals[0][1] = 180;
  build_pad_property (&dimension[0], dim_vals[0], 2);

  g_object_set_property (G_OBJECT (composer_sink[3]),
      "dimensions", &dimension[0]);

  // Setting alpha channel value for Segmentation overlay window
  alpha_value = 0.5;
  g_object_set (composer_sink[7], "alpha", &alpha_value, NULL);

  for (int i=0; i<8; i++) {
    gst_object_unref (composer_sink[i]);
  }

  for (int i = 0; i < PIPELINE_CNT; i++) {
    g_value_unset (&position[i]);
    g_value_unset (&dimension[i]);
  }

  return TRUE;

  // For any errors in plugin creation, cleanup earlier created ones
error:
  gst_bin_remove_many (GST_BIN (appctx->pipeline), qtiqmmfsrc, main_capsfilter,
      tee, qtivcomposer, waylandsink, NULL);

  for (int i = 0; i < PIPELINE_CNT; i++) {
    gst_bin_remove_many (GST_BIN (appctx->pipeline), qtimlelement[i],
        qtimlvpostproc[i], qtimlvconverter[i], detection_filter[i], NULL);
  }

  for (int i = 0; i < QUEUE_COUNT; i++) {
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

  GList *list = appctx->plugins->next;
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

/**
 * Main Function Of the Application.
 */
gint
main (gint argc, gchar * argv[])
{
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  GstElement *pipeline = NULL;
  const char *app_name = strrchr (argv[0], '/') ?
      (strrchr (argv[0], '/') + 1) : argv[0];
  GstAppContext appctx = {};
  guint intrpt_watch_id = 0;
  gboolean ret = FALSE;

  if (argc > 1) {
    help (app_name);
    return -1;
  }

  // Initialize GST library.
  argc = 1;
  gst_init (&argc, &argv);

  // Create the pipeline that will form connection with other elements
  pipeline = gst_pipeline_new (app_name);
  if (!pipeline) {
    g_printerr ("ERROR: failed to create pipeline.\n");
    return -1;
  }

  appctx.pipeline = pipeline;

  // Build the pipeline, link all elements in the pipeline
  ret = create_pipe (&appctx);
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

  // Call respective callback function based on message
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
  g_print ("Setting pipeline to PAUSED state ...\n");
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

  g_print ("Setting pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Destory pipeline\n");
  destroy_pipe (&appctx);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}
