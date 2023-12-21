/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

/*
* Application:
* GStreamer Concurrent video playback for HEVC and AVC codec.
*
* Description:
* This is an application of Concurrent 2 Video playback for HEVC and AVC codec
*
* Usage:
* gst-concurrent-videoplayback
*
* Help:
* gst-concurrent-videoplayback --help
*/

#include <errno.h>
#include <stdio.h>
#include <glib-unix.h>
#include <gst/gst.h>

#define ARRAY_LENGTH 20
#define TWO_STREAM_CNT 2
#define AVC_FILESOURCE "/opt/Animals_000_720p_180s_30FPS.mp4"
#define HEVC_FILESOURCE "/opt/Animals_000_720p_180s_30FPS.mp4"
#define YUV_FILESINK "/opt/1280_720_test2.yuv"


#define GST_PIPELINE_2STREAM_VIDEO  "filesrc name=source1 " \
  "location=AVC_FILESOURCE ! qtdemux ! queue ! h264parse ! " \
  "v4l2h264dec capture-io-mode=5 output-io-mode=5 ! " \
  "queue ! waylandsink enable-last-sample=false async=false fullscreen=true " \
  "filesrc name=source2 location=HEVC_FILESOURCE ! qtdemux ! h265parse ! " \
  "v4l2h265dec capture-io-mode=5 output-io-mode=5 ! " \
  "filesink name=sink_yuv enable-last-sample=false location=YUV_FILESINK " \

typedef struct _GstAppContext GstAppContext;

struct _GstAppContext
{
  // Pointer to the pipeline
  GstElement *pipeline;
  // Pointer to the mainloop
  GMainLoop *mloop;
};

static GstAppContext *
gst_app_context_new ()
{
  GstAppContext *ctx = (GstAppContext *)g_new0 (GstAppContext, 1);

  if (NULL == ctx) {
    g_printerr ("Unable to create App Context");
    return NULL;
  }

  ctx->pipeline = NULL;
  ctx->mloop    = NULL;
  return ctx;
}

static void
gst_app_context_free (GstAppContext * ctx, gchar **in_files, gchar **out_file)
{
  if (ctx->mloop != NULL) {
    g_main_loop_unref (ctx->mloop);
    ctx->mloop = NULL;
  }

  if (ctx->pipeline != NULL) {
    gst_element_set_state (ctx->pipeline, GST_STATE_NULL);
    gst_object_unref (ctx->pipeline);
    ctx->pipeline = NULL;
  }

  if (in_files)
    g_strfreev (in_files);

  if (out_file)
    g_strfreev (out_file);
  g_free (ctx);
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

// Create all elements and link
static gboolean
create_pipe (GstAppContext *appctx, gint stream_cnt,
                gchar **input_files_arg, gchar **output_files_arg)
{
  GstBus *bus = NULL;
  GError *error = NULL;
  GstElement *element = NULL;
  GstElement *sink = NULL;
  gchar temp_str[ARRAY_LENGTH];

  // Initiate an empty pipeline
  if (stream_cnt == TWO_STREAM_CNT) {
      appctx->pipeline = gst_parse_launch (GST_PIPELINE_2STREAM_VIDEO, &error);
  }

  if (appctx->pipeline == NULL) {
    if (NULL != error) {
      g_printerr ("Pipeline couldn't be created, error %s",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
    }
    return FALSE;
  }

  for (int i = 1; i <= stream_cnt; i++)
  {
    snprintf (temp_str, sizeof (temp_str), "source%d", i);
    element = gst_bin_get_by_name (GST_BIN (appctx->pipeline), temp_str);

    if (element != NULL) {
      if (input_files_arg == NULL) {
        g_printerr ("Couldn't find input files\n");
        return FALSE;
      } else {
        g_object_set (G_OBJECT (element), "location", input_files_arg[i-1], NULL);
        gst_object_unref (element);
      }
    } else {
      g_printerr ("Couldn't find filesrc\n");
      return FALSE;
    }
    memset( temp_str, 0, ARRAY_LENGTH );
  }

  // Set output file path
  sink = gst_bin_get_by_name (GST_BIN (appctx->pipeline), "sink_yuv");
  if (sink != NULL) {
    if (output_files_arg == NULL) {
      g_printerr ("Couldn't find output file path\n");
      return FALSE;
    } else {
      g_object_set (G_OBJECT (sink), "location", output_files_arg[0], NULL);
      gst_object_unref (sink);
    }
  } else {
    g_printerr ("Couldn't find filesrc\n");
    return FALSE;
  }

  return TRUE;
}

gint
main (gint argc, gchar * argv[])
{
  GOptionContext *ctx = NULL;
  GMainLoop *mloop = NULL;
  GstBus *bus = NULL;
  guint intrpt_watch_id = 0;
  gboolean ret = FALSE;
  gint stream_cnt = TWO_STREAM_CNT;
  GstAppContext *appctx = NULL;
  GstElement * element = NULL;
  gchar **input_files_arg = NULL;
  gchar **output_files_arg = NULL;

  // Configure input parameters
  GOptionEntry entries[] = {
    { "stream_cnt", 'c', 0,
      G_OPTION_ARG_INT, &stream_cnt,
      "No of stream for video playback First is AVC and \
          Second is HEVC file 2 - 2 stream, default - 2"
    },
    { "input_file", 'i', 0,
      G_OPTION_ARG_FILENAME_ARRAY, &input_files_arg,
      "Input Filenames - need to give 2 mp4 files one is AVC and \
          other HEVC codec in order, \
          -i /opt/1280_720_H264_30fps.mp4 \
          -i /opt/1280_720_H265_30fps.mp4"
    },
    { "output_file", 'o', 0,
      G_OPTION_ARG_FILENAME_ARRAY, &output_files_arg,
      "Output Filename , \
          -o /opt/1280_720_test2.yuv "
    },
    { NULL }
  };

  // Parse command line entries.
  if ((ctx = g_option_context_new (
      "Concurrent Video playback for AVC and HEVC codec ")) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_add_main_entries (ctx, entries, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());

    success = g_option_context_parse (ctx, &argc, &argv, &error);
    g_option_context_free (ctx);

    if (!success && (error != NULL)) {
      g_printerr ("ERROR: Failed to parse command line options: %s!\n",
           GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -EFAULT;
    } else if (!success && (NULL == error)) {
      g_printerr ("ERROR: Initializing: Unknown error!\n");
      return -EFAULT;
    }
  } else {
    g_printerr ("ERROR: Failed to create options context!\n");
    return -EFAULT;
  }

  // Initialize GST library.
  gst_init (&argc, &argv);

  appctx = gst_app_context_new ();

  if (NULL == appctx)
    return -1;

  // Build the pipeline
  ret = create_pipe (appctx, stream_cnt, input_files_arg, output_files_arg);
  if (!ret) {
    g_printerr ("Failed to create GST pipe.\n");
    gst_app_context_free (appctx, input_files_arg, output_files_arg);
    return -1;
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    gst_app_context_free (appctx, input_files_arg, output_files_arg);
    return -1;
  }
  appctx->mloop = mloop;

  // Retrieve reference to the pipeline's bus.
  if ((bus = gst_pipeline_get_bus (GST_PIPELINE (appctx->pipeline))) == NULL) {
    g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");
    gst_app_context_free (appctx, input_files_arg, output_files_arg);
    return -1;
  }

  // Watch for messages on the pipeline's bus.
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::state-changed",
      G_CALLBACK (state_changed_cb), appctx->pipeline);
  g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);
  gst_object_unref (bus);

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id =
      g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  g_print ("Setting pipeline to PAUSED state ...\n");
  switch (gst_element_set_state (appctx->pipeline, GST_STATE_PAUSED)) {
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

  g_source_remove (intrpt_watch_id);

  g_print ("Destory pipeline\n");
  gst_app_context_free (appctx, input_files_arg, output_files_arg);

  g_print ("gst_deinit\n");
  gst_deinit ();

  return 0;
}

