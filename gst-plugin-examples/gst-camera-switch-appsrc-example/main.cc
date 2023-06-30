/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <glib-unix.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#define GST_APP_DESCRIPTION \
  "This application uses the two cameras of the device and switch them using\n"\
  "different pipelines with appsink. The switching is done every 5 seconds.\n"\
  "Additional pipeline with appsrc using the camera buffers and send them to\n"\
  "next plugins."

typedef struct _GstAppContext GstAppContext;

// Contains application context information.
struct _GstAppContext
{
  // Main application event loop.
  GMainLoop   *mloop;

  // Main pipeline which receives the buffers from the camera pipelines.
  GstElement  *m_pipeline;
  // Array of camera source pipelines.
  GPtrArray   *s_pipelines;

  GMutex      lock;
};

/// Command line option variables.
static gboolean use_display  = FALSE;
static gint     m_camera_id  = 0;
static gint     s_camera_id  = 1;
static gint     video_width  = 1280;
static gint     video_height = 720;
static gint     switch_delay = 5000;

static const GOptionEntry entries[] = {
    { "display", 'd', 0, G_OPTION_ARG_NONE, &use_display,
      "Video stream is directed to display instead of saving it into a file.",
      NULL
    },
    { "main-camera", 'm', 0, G_OPTION_ARG_INT, &m_camera_id,
      "ID of the main camera (default: 0).", NULL
    },
    { "secondary-camera", 's', 0, G_OPTION_ARG_INT, &s_camera_id,
      "ID of the secondary camera (default: 1).", NULL
    },
    { "width", 'w', 0, G_OPTION_ARG_INT, &video_width,
      "Video stream width (default: 1280).", NULL
    },
    { "height", 'h', 0, G_OPTION_ARG_INT, &video_height,
      "Video stream height (default: 720).", NULL
    },
    { "delay", 'l', 0, G_OPTION_ARG_INT, &switch_delay,
      "Camera switch delay in ms (default: 5000).", NULL
    },
    { NULL }
};

static GstAppContext *
gst_app_context_new ()
{
  GstAppContext *ctx = g_new0 (GstAppContext, 1);

  ctx->mloop = NULL;
  ctx->s_pipelines =
      g_ptr_array_new_with_free_func ((GDestroyNotify) gst_object_unref);
  ctx->m_pipeline = NULL;

  g_mutex_init (&ctx->lock);
  return ctx;
}

static void
gst_app_context_free (GstAppContext * ctx)
{
  if (ctx->mloop != NULL)
    g_main_loop_unref (ctx->mloop);

  if (ctx->m_pipeline != NULL)
    gst_object_unref (ctx->m_pipeline);

  g_ptr_array_free (ctx->s_pipelines, TRUE);
  g_mutex_clear (&ctx->lock);

  g_free (ctx);
  return;
}

static void
gst_sample_release (GstSample * sample)
{
    gst_sample_unref (sample);
#if GST_VERSION_MAJOR >= 1 && GST_VERSION_MINOR > 14
    gst_sample_set_buffer (sample, NULL);
#endif
}

// Hangles interrupt signals like Ctrl+C etc.
static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  GstState state = GST_STATE_VOID_PENDING;
  static gboolean waiting_eos = FALSE;

  g_mutex_lock (&appctx->lock);

  // Get the current state of the pipeline.
  gst_element_get_state (appctx->m_pipeline, &state, NULL, 0);

  if (!waiting_eos && (state == GST_STATE_PLAYING)) {
    GstElement *pipeline = NULL;
    guint idx = 0;

    g_print ("\n\nReceived an interrupt signal, send EOS ...\n");
    gst_element_send_event (appctx->m_pipeline, gst_event_new_eos ());

    for (idx = 0; idx < appctx->s_pipelines->len; ++idx) {
      pipeline = GST_ELEMENT (g_ptr_array_index (appctx->s_pipelines, idx));
      gst_element_get_state (pipeline, &state, NULL, 0);

      if (state == GST_STATE_PLAYING)
        gst_element_send_event (pipeline, gst_event_new_eos ());
    }

    g_print ("\nWaiting for EOS...\n");
    waiting_eos = TRUE;
  } else if (waiting_eos) {
    g_print ("\nInterrupt while waiting for EOS - quit main loop...\n");
    g_main_loop_quit (appctx->mloop);

    waiting_eos = FALSE;
  } else {
    g_print ("\n\nReceived an interrupt signal, stopping pipeline ...\n");
    g_main_loop_quit (appctx->mloop);
  }

  g_mutex_unlock (&appctx->lock);

  return TRUE;
}

// Handles state change transisions
static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, newstate, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &newstate, &pending);
  g_print ("\n'%s' state changed from %s to %s, pending: %s\n",
      GST_ELEMENT_NAME (pipeline), gst_element_state_get_name (old),
      gst_element_state_get_name (newstate), gst_element_state_get_name (pending));
}

// Handle warnings
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

// Handle errors
static void
error_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;
  GError *error = NULL;
  gchar *debug = NULL;

  gst_message_parse_error (message, &error, &debug);
  gst_object_default_error (GST_MESSAGE_SRC (message), error, debug);

  g_free (debug);
  g_error_free (error);

  g_main_loop_quit (appctx->mloop);
}

// Handle End-Of-Stream
static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext *) userdata;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));

  g_main_loop_quit (appctx->mloop);
}

static GstFlowReturn
new_sample (GstElement * sink, gpointer userdata)
{
  GstElement *appsrc = (GstElement*) userdata;
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  static GstClockTime timestamp = GST_CLOCK_TIME_NONE;

  // New sample is available, retrieve the buffer from the sink.
  g_signal_emit_by_name (sink, "pull-sample", &sample);

  if (sample == NULL) {
    g_printerr ("ERROR: Pulled sample is NULL!\n");
    return GST_FLOW_ERROR;
  }

  if ((buffer = gst_sample_get_buffer (sample)) == NULL) {
    g_printerr ("ERROR: Pulled buffer is NULL!\n");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  // Get first timestamp
  if (timestamp == GST_CLOCK_TIME_NONE)
    timestamp = GST_BUFFER_PTS (buffer);

  GST_BUFFER_PTS (buffer) = timestamp;
  timestamp += GST_BUFFER_DURATION (buffer);

  gst_app_src_push_sample (GST_APP_SRC (appsrc), sample);
  gst_sample_release (sample);

  return GST_FLOW_OK;
}

static gboolean
update_pipeline_state (GstElement * pipeline, GstState state)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  GstState current, pending;

  // First check current and pending states of the pipeline.
  ret = gst_element_get_state (pipeline, &current, &pending, 0);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Failed to retrieve '%s' state!\n", GST_ELEMENT_NAME (pipeline));
    return FALSE;
  }

  if (state == current) {
    g_print ("'%s' already in %s state\n", GST_ELEMENT_NAME (pipeline),
        gst_element_state_get_name (state));
    return TRUE;
  } else if (state == pending) {
    g_print ("'%s' pending %s state\n", GST_ELEMENT_NAME (pipeline),
        gst_element_state_get_name (state));
    return TRUE;
  }

  g_print ("Setting '%s' to %s\n", GST_ELEMENT_NAME (pipeline),
      gst_element_state_get_name (state));
  ret = gst_element_set_state (pipeline, state);

  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: '%s' failed to transition to %s state!\n",
          GST_ELEMENT_NAME (pipeline), gst_element_state_get_name (state));
      return TRUE;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("'%s' is live and does not need PREROLL.\n",
          GST_ELEMENT_NAME (pipeline));
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("'%s' is PREROLLING ...\n", GST_ELEMENT_NAME (pipeline));

      ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

      if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr ("'%s' failed to PREROLL!\n", GST_ELEMENT_NAME (pipeline));
        return FALSE;
      }
      break;
    case GST_STATE_CHANGE_SUCCESS:
      g_print ("'%s' state change was successful\n", GST_ELEMENT_NAME (pipeline));
      break;
  }

  return TRUE;
}

static gboolean
create_main_pipeline (GstAppContext * appctx, const gboolean display,
    const guint width, const guint height)
{
  GstElement *pipeline = NULL, *element = NULL;
  GError *error = NULL;
  GstCaps *filtercaps = NULL;
  GstBus *bus = NULL;
  const gchar *description = NULL;

  if (display) {
    description = "appsrc name=appsrc ! queue ! waylandsink name=waylandsink";
  } else {
#ifdef CODEC2_ENCODE
    description = "appsrc name=appsrc ! queue ! qtic2venc name=c2venc ! "
        "h264parse ! mp4mux ! queue ! filesink name=filesink";
#else
    description = "appsrc name=appsrc ! queue ! omxh264enc name=omxvenc ! "
        "h264parse ! mp4mux ! queue ! filesink name=filesink";
#endif // CODEC2_ENCODE
  }

  pipeline = gst_parse_launch (description, &error);

  // Check for errors on pipe creation.
  if ((NULL == pipeline) && (error != NULL)) {
    g_printerr ("ERROR: Failed to create pipeline, error: %s!\n",
        GST_STR_NULL (error->message));
    g_clear_error (&error);
    return FALSE;
  } else if ((NULL == pipeline) && (NULL == error)) {
    g_printerr ("ERROR: Failed to create pipeline, unknown error!\n");
    return FALSE;
  } else if ((pipeline != NULL) && (error != NULL)) {
    g_printerr ("ERROR: Erroneous pipeline, error: %s!\n",
        GST_STR_NULL (error->message));
    g_clear_error (&error);
    return FALSE;
  }

  g_object_set (G_OBJECT (pipeline), "name", "Main Pipeline", NULL);

  // Set properties for appsrc element.
  element = gst_bin_get_by_name (GST_BIN (pipeline), "appsrc");

  g_object_set (G_OBJECT (element),
      "stream-type", GST_APP_STREAM_TYPE_STREAM,
      "format", GST_FORMAT_TIME,
      "is-live", TRUE,
      NULL);

  filtercaps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, "NV12",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);

  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));
  g_object_set (G_OBJECT (element), "caps", filtercaps, NULL);

  gst_caps_unref (filtercaps);
  gst_object_unref (element);

  // Set properties for waylandsink element if present.
  if (element = gst_bin_get_by_name (GST_BIN (pipeline), "waylandsink")) {
    g_object_set (G_OBJECT (element), "x", 0, NULL);
    g_object_set (G_OBJECT (element), "y", 0, NULL);
    g_object_set (G_OBJECT (element), "width", 600, NULL);
    g_object_set (G_OBJECT (element), "height", 400, NULL);

    g_object_set (G_OBJECT (element), "async", FALSE, NULL);
    g_object_set (G_OBJECT (element), "sync", FALSE, NULL);
    g_object_set (G_OBJECT (element), "enable-last-sample", FALSE, NULL);

    gst_object_unref (element);
  }

  // Set properties for encoder element if present.
  if (element = gst_bin_get_by_name (GST_BIN (pipeline), "c2venc")) {
    g_object_set (G_OBJECT (element), "target-bitrate", 6000000, NULL);
    g_object_set (G_OBJECT (element), "control-rate", 3, NULL); // VBR-CFR

    gst_object_unref (element);
  } else if (element = gst_bin_get_by_name (GST_BIN (pipeline), "omxvenc")) {
    g_object_set (G_OBJECT (element), "target-bitrate", 6000000, NULL);
    g_object_set (G_OBJECT (element), "periodicity-idr", 1, NULL);
    g_object_set (G_OBJECT (element), "interval-intraframes", 29, NULL);
    g_object_set (G_OBJECT (element), "control-rate", 2, NULL);

    gst_object_unref (element);
  }

  // Set properties for filesink element if present.
  if (element = gst_bin_get_by_name (GST_BIN (pipeline), "filesink")) {
    g_object_set (G_OBJECT (element), "location", "/data/mux.mp4", NULL);
    g_object_set (G_OBJECT (element), "enable-last-sample", FALSE, NULL);

    gst_object_unref (element);
  }

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
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), appctx);
  g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), appctx);
  gst_object_unref (bus);

  // Set the newly created pipeline in the application context.
  appctx->m_pipeline = pipeline;

  return TRUE;

cleanup:
  if (filtercaps != NULL)
    gst_caps_unref (filtercaps);

  if (pipeline != NULL)
    gst_object_unref (pipeline);

  return FALSE;
}

static gboolean
create_camera_pipeline (GstAppContext * appctx, const guint cam_id,
    const guint width, const guint height)
{
  GstElement *pipeline = NULL, *camsrc = NULL, *appsink = NULL, *appsrc = NULL;
  GstCaps *filtercaps = NULL;
  GstBus *bus = NULL;
  gchar *name = NULL;

  name = g_strdup_printf ("Camera %d Pipeline", cam_id);

  pipeline = gst_pipeline_new (name);
  g_free (name);

  g_return_val_if_fail (pipeline != NULL, FALSE);

  if ((camsrc = gst_element_factory_make ("qtiqmmfsrc", "camsrc")) == NULL) {
    g_printerr ("ERROR: Failed to create 'qtiqmmfsrc' !\n");
    goto cleanup;
  }

  if ((appsink = gst_element_factory_make ("appsink", "appsink")) == NULL) {
    g_printerr ("ERROR: Failed to create 'qtiqmmfsrc' or 'appsink' !\n");
    gst_object_unref (camsrc);
    goto cleanup;
  }

  gst_bin_add_many (GST_BIN (pipeline), camsrc, appsink, NULL);

  g_object_set (G_OBJECT (camsrc), "camera", cam_id, NULL);
  g_object_set (G_OBJECT (appsink), "emit-signals", 1, NULL);
  g_object_set (G_OBJECT (appsink), "enable-last-sample", FALSE, NULL);

  filtercaps = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, width, "height", G_TYPE_INT, height,
      "format", G_TYPE_STRING, "NV12", "framerate", GST_TYPE_FRACTION, 30, 1,
      NULL);
  gst_caps_set_features (filtercaps, 0,
      gst_caps_features_new ("memory:GBM", NULL));

  if (!gst_element_link_filtered (camsrc, appsink, filtercaps)) {
    g_printerr ("ERROR: Failed to link elements in pipeline!\n");
    goto cleanup;
  }

  gst_caps_unref (filtercaps);

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
  g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), appctx);
  gst_object_unref (bus);

  // Connect a callback to the appsink new-sample signal with appsrc as argument.
  appsrc = gst_bin_get_by_name (GST_BIN (appctx->m_pipeline), "appsrc");
  g_signal_connect (appsink, "new-sample", G_CALLBACK (new_sample), appsrc);
  gst_object_unref (appsrc);

  // Add the newly created pipeline to the list with source pipelines.
  g_ptr_array_add (appctx->s_pipelines, pipeline);

  return TRUE;

cleanup:
  if (filtercaps != NULL)
    gst_caps_unref (filtercaps);

  if (pipeline != NULL)
    gst_object_unref (pipeline);

  return FALSE;
}

static gboolean
handle_switch_event (gpointer userdata)
{
  GstAppContext *appctx = (GstAppContext*) userdata;
  GstElement *pipeline = NULL;
  GstBus *bus = NULL;
  GstMessage *message = NULL;
  gboolean success = TRUE;
  static guint index = 0;

  g_mutex_lock (&appctx->lock);

  g_print ("Stopping pipeline at index %u ...\n", index);
  pipeline = GST_ELEMENT (g_ptr_array_index (appctx->s_pipelines, index));
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  gst_element_send_event (pipeline, gst_event_new_eos ());

  if (use_display) {
    gst_element_send_event (appctx->m_pipeline, gst_event_new_flush_start ());
    gst_element_send_event (appctx->m_pipeline, gst_event_new_flush_stop (FALSE));
  } else {
    // Due to limitations in mainstream filesink where the current postion in
    // the file is reset to 0 at FLUSH_STOP (ignoring the reset_time flag) we
    // have to get the current position and send a SEEK with FLUSH event.
    gint64 position = 0;

    gst_element_query_position (appctx->m_pipeline, GST_FORMAT_TIME, &position);
    g_print ("\nPosition %" G_GINT64_FORMAT"\n", position);

    gst_element_seek_simple (appctx->m_pipeline, GST_FORMAT_TIME,
        GST_SEEK_FLAG_FLUSH, position);
  }

  // Wait for EOS to be acknowledged on the pipeline bus.
  message = gst_bus_timed_pop_filtered (bus, GST_SECOND, GST_MESSAGE_EOS);
  g_print ("\nReceived End-of-Stream from '%s'\n", GST_MESSAGE_SRC_NAME (message));

  gst_message_unref (message);
  gst_object_unref (bus);

  if (!(success = update_pipeline_state (pipeline, GST_STATE_NULL))) {
    g_main_loop_quit (appctx->mloop);
    goto exit;
  }

  g_print ("Stopped pipeline at index %u\n", index);
  index = ((index + 1) < appctx->s_pipelines->len) ? (index + 1) : 0;

  g_print ("Start pipeline at index %u ...\n", index);
  pipeline = GST_ELEMENT (g_ptr_array_index (appctx->s_pipelines, index));

  if (!(success = update_pipeline_state (pipeline, GST_STATE_PLAYING))) {
    g_main_loop_quit (appctx->mloop);
    goto exit;
  }

  g_print ("Started pipeline at index %u\n", index);

exit:
  g_mutex_unlock (&appctx->lock);

  return success;
}

gint
main (gint argc, gchar * argv[])
{
  GstAppContext *appctx = NULL;
  GOptionContext *optsctx = NULL;
  GstElement *pipeline = NULL;
  guint idx = 0, intrpt_watch_id = 0, event_watch_id = 0;

  // Initialize GST library.
  gst_init (&argc, &argv);

  // Parse command line entries.
  if ((optsctx = g_option_context_new (NULL)) != NULL) {
    gboolean success = FALSE;
    GError *error = NULL;

    g_option_context_set_summary (optsctx, GST_APP_DESCRIPTION);
    g_option_context_add_main_entries (optsctx, entries, NULL);
    g_option_context_add_group (optsctx, gst_init_get_option_group ());

    success = g_option_context_parse (optsctx, &argc, &argv, &error);
    g_option_context_free (optsctx);

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

  // Create Application context.
  appctx = gst_app_context_new ();
  g_return_val_if_fail (appctx != NULL, -EFAULT);

  // Create Main pipeline.
  if (!create_main_pipeline (appctx, use_display, video_width, video_height)) {
    g_printerr ("ERROR: Failed to create main pipeline!\n");
    goto cleanup;
  }

  // Create Camera 0 pipeline.
  if (!create_camera_pipeline (appctx, m_camera_id, video_width, video_height)) {
    g_printerr ("ERROR: Failed to create camera %d  pipeline!\n", m_camera_id);
    goto cleanup;
  }

  // Create Camera 1 pipeline.
  if (!create_camera_pipeline (appctx, s_camera_id, video_width, video_height)) {
    g_printerr ("ERROR: Failed to create camera %d pipeline!\n", s_camera_id);
    goto cleanup;
  }

  // Initialize main loop.
  if ((appctx->mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    return -EFAULT;
  }

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, appctx);

  g_print ("Setting Main Pipeline to PLAYING state ...\n");

  switch (gst_element_set_state (appctx->m_pipeline, GST_STATE_PLAYING)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PLAYING state!\n");
      goto cleanup;
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

  g_print ("Setting Camera %d Pipeline to PLAYING state ...\n", m_camera_id);
  pipeline = GST_ELEMENT (g_ptr_array_index (appctx->s_pipelines, 0));

  switch (gst_element_set_state (pipeline, GST_STATE_PLAYING)) {
    case GST_STATE_CHANGE_FAILURE:
      g_printerr ("ERROR: Failed to transition to PLAYING state!\n");
      goto cleanup;
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

  // Register a timed source which will do the switch at set intervals.
  event_watch_id = g_timeout_add (switch_delay, handle_switch_event, appctx);

  // Run main loop.
  g_main_loop_run (appctx->mloop);

  g_source_remove (event_watch_id);

  g_print ("Setting pipelines to NULL state ...\n");
  gst_element_set_state (appctx->m_pipeline, GST_STATE_NULL);

  for (idx = 0; idx < appctx->s_pipelines->len; ++idx) {
    pipeline = GST_ELEMENT (g_ptr_array_index (appctx->s_pipelines, idx));
    gst_element_set_state (pipeline, GST_STATE_NULL);
  }

  g_source_remove (intrpt_watch_id);

cleanup:
  gst_app_context_free (appctx);
  gst_deinit ();

  return 0;
}
