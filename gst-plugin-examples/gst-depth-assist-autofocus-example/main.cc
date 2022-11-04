/*
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include <camera/CameraMetadata.h>
#include <camera/VendorTagDescriptor.h>

#define PROPERTY_VALUE_MAX 32
#define DEFAULT_30_FPS 33
#define DEFAULT_DEPTH_VALID 1
#define DEFAULT_DEPTH_DISTANCE 10000
#define DEFAULT_DEPTH_DISTANCE_CONFIDENCE 2
#define DEFAULT_DEPTH_DISTANCE_NEAR_LIMITATION 100
#define DEFAULT_DEPTH_DISTANCE_FAR_LIMITATION  10000

typedef struct _GstMetadataUpdate GstMetadataUpdate;
struct _GstMetadataUpdate
{
  // Pointer to the pipeline
  GstElement *pipeline;

  // Pointer to the qmmfsrc
  GstElement *qtiqmmfsrc;

  // Pointer to the mainloop
  GMainLoop *mloop;

  GMutex update_lock;
  GCond update_signal;

  gint finish;
};

static void
gst_sample_release (GstSample * sample)
{
    gst_sample_unref (sample);
#if GST_VERSION_MAJOR >= 1 && GST_VERSION_MINOR > 14
    gst_sample_set_buffer (sample, NULL);
#endif
}

static gboolean
handle_interrupt_signal (gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);

  g_print ("\n\nReceived an interrupt signal, quit main loop ...\n");
  gst_element_send_event (pipeline, gst_event_new_eos ());

  return TRUE;
}

static void
state_changed_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GstElement *pipeline = GST_ELEMENT (userdata);
  GstState old, newst, pending;

  // Handle state changes only for the pipeline.
  if (GST_MESSAGE_SRC (message) != GST_OBJECT_CAST (pipeline))
    return;

  gst_message_parse_state_changed (message, &old, &newst, &pending);
  g_print ("\nPipeline state changed from %s to %s, pending: %s\n",
      gst_element_state_get_name (old), gst_element_state_get_name (newst),
      gst_element_state_get_name (pending));

  if ((newst == GST_STATE_PAUSED) && (old == GST_STATE_READY) &&
      (pending == GST_STATE_VOID_PENDING)) {
    g_print ("\nSetting pipeline to PLAYING state ...\n");

    if (gst_element_set_state (pipeline,
            GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
      gst_printerr ("\nPipeline doesn't want to transition to PLAYING state!\n");
      return;
    }
  }
}

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

static guint
get_vendor_tag_by_name (const gchar * section, const gchar * name)
{
  ::android::sp<::android::VendorTagDescriptor> vtags;
  ::android::status_t status = 0;
  guint tag_id = 0;

  vtags = ::android::VendorTagDescriptor::getGlobalVendorTagDescriptor();
  if (vtags.get() == NULL) {
    GST_WARNING ("Failed to retrieve Global Vendor Tag Descriptor!");
    return 0;
  }

  status = vtags->lookupTag(::android::String8(name),
      ::android::String8(section), &tag_id);
  if (status != 0) {
    GST_WARNING ("Unable to locate tag for '%s', section '%s'!", name, section);
    return 0;
  }

  return tag_id;
}

static GstFlowReturn
new_sample (GstElement *sink, gpointer userdata)
{
  GstSample *sample = NULL;
  GstBuffer *buffer = NULL;
  guint64 timestamp = 0;
  GstMapInfo info;

  // New sample is available, retrieve the buffer from the sink.
  g_signal_emit_by_name (sink, "pull-sample", &sample);

  if (sample == NULL) {
    g_printerr ("ERROR: Pulled sample is NULL!");
    return GST_FLOW_ERROR;
  }

  if ((buffer = gst_sample_get_buffer (sample)) == NULL) {
    g_printerr ("ERROR: Pulled buffer is NULL!");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ)) {
    g_printerr ("ERROR: Failed to map the pulled buffer!");
    gst_sample_release (sample);
    return GST_FLOW_ERROR;
  }

  // Extract the original camera timestamp from GstBuffer OFFSET_END field
  timestamp = GST_BUFFER_OFFSET_END (buffer);
  g_print ("Camera timestamp: %" G_GUINT64_FORMAT "\n", timestamp);

  gst_buffer_unmap (buffer, &info);
  gst_sample_release (sample);

  return GST_FLOW_OK;
}

static GstFlowReturn
result_metadata (gpointer userdata, guint camera_id, gpointer metadata)
{
  ::android::CameraMetadata *meta_ptr = (::android::CameraMetadata*) metadata;
  camera_metadata_entry entry;

  if (meta_ptr != nullptr) {
    g_print ("Result metadata ... entries - %ld\n", meta_ptr->entryCount());

    // Exposure time
    if (meta_ptr->exists(ANDROID_SENSOR_EXPOSURE_TIME)) {
      gint64 sensorExpTime =
          meta_ptr->find(ANDROID_SENSOR_EXPOSURE_TIME).data.i64[0];
      g_print ("Result sensor_exp_time - %ld\n", sensorExpTime);
    }
    // Sensor Timestamp
    if (meta_ptr->exists(ANDROID_SENSOR_TIMESTAMP)) {
      gint64 timestamp =
          meta_ptr->find(ANDROID_SENSOR_TIMESTAMP).data.i64[0];
      g_print ("Result timestamp - %ld\n", timestamp);
    }
  }

  return GST_FLOW_OK;
}

static void
eos_cb (GstBus * bus, GstMessage * message, gpointer userdata)
{
  GMainLoop *mloop = (GMainLoop*) userdata;

  g_print ("\nReceived End-of-Stream from '%s' ...\n",
      GST_MESSAGE_SRC_NAME (message));

  g_main_loop_quit (mloop);
}

static gpointer
metadata_update_thread (void * data)
{
  gint32   value = 0;
  gint64   timestamp = 0;
  guint    tag_id = 0;

  GstMetadataUpdate *metadata_update = (GstMetadataUpdate *) data;

  while (!metadata_update->finish) {
    g_mutex_lock (&metadata_update->update_lock);

    //waiting to timeout
    gint64 wait_time = g_get_monotonic_time () +
        DEFAULT_30_FPS* G_TIME_SPAN_MILLISECOND;
    gboolean timeout = g_cond_wait_until (&metadata_update->update_signal,
        &metadata_update->update_lock, wait_time);
    if ((!timeout) && (!metadata_update->finish)) {

      // Get capture metadata
      ::android::CameraMetadata *meta = nullptr;
      g_object_get (G_OBJECT (metadata_update->qtiqmmfsrc),
          "capture-metadata", &meta, NULL);
      if (meta) {
        g_print ("Get capture-metadata entries - %ld\n",
            meta->entryCount());

        // Set auto focus mode
        guchar afmode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
        meta->update(ANDROID_CONTROL_AF_MODE, &afmode, 1);

        //Set depth assist af valid flag
        value = DEFAULT_DEPTH_VALID;
        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.depthassistafinput", "isvalid");
        meta->update(tag_id, &value, 1);

        // Calculated object distance in mm
        value = DEFAULT_DEPTH_DISTANCE;
        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.depthassistafinput", "distanceInMilliMeters");
        meta->update(tag_id, &value, 1);

        //Set object distance confidence level
        value = DEFAULT_DEPTH_DISTANCE_CONFIDENCE;
        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.depthassistafinput", "confidence");
        meta->update(tag_id, &value, 1);

        //Set min object distance measured
        value = DEFAULT_DEPTH_DISTANCE_NEAR_LIMITATION;
        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.depthassistafinput", "nearLimitation");
        meta->update(tag_id, &value, 1);

        //Set max distanc measured
        value = DEFAULT_DEPTH_DISTANCE_FAR_LIMITATION;
        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.depthassistafinput", "farLimitation");
        meta->update(tag_id, &value, 1);

        // Set timestamp of arrival of the laser data
        timestamp = g_get_monotonic_time ();
        tag_id = get_vendor_tag_by_name (
            "org.codeaurora.qcamera3.depthassistafinput", "timestamp");
        meta->update(tag_id, &timestamp, 1);

        g_print ("depth timestamp %lld \n",timestamp);

        g_object_set (G_OBJECT (metadata_update->qtiqmmfsrc),
            "capture-metadata", meta, NULL);
      } else {
        g_print ("Get capture-metadata failed!\n");
      }
    }

    g_mutex_unlock (&metadata_update->update_lock);
  }

  g_print ("Thread exit\n");
  return NULL;
}

gint
main (gint argc, gchar *argv[])
{
  GstElement *pipeline = NULL;
  GMainLoop *mloop = NULL;
  guint intrpt_watch_id = 0;
  GstMetadataUpdate metadata_update = {};

  g_set_prgname ("gst-depth-assist-autofocus-example");

  // Initialize GST library.
  gst_init (&argc, &argv);

  {
    GError *error = NULL;

    pipeline = gst_parse_launch ("qtiqmmfsrc name=camera ! \
        video/x-raw(memory:GBM),format=NV12,width=1280,height=720,framerate=30/1 ! \
        queue ! appsink name=sink emit-signals=true",
        &error);

    // Check for errors on pipe creation.
    if ((NULL == pipeline) && (error != NULL)) {
      g_printerr ("Failed to create pipeline, error: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return -1;
    } else if ((NULL == pipeline) && (NULL == error)) {
      g_printerr ("Failed to create pipeline, unknown error!\n");
      return -1;
    } else if ((pipeline != NULL) && (error != NULL)) {
      g_printerr ("Erroneous pipeline, error: %s!\n",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      gst_object_unref (pipeline);
      return -1;
    }
  }

  // Initialize main loop.
  if ((mloop = g_main_loop_new (NULL, FALSE)) == NULL) {
    g_printerr ("ERROR: Failed to create Main loop!\n");
    gst_object_unref (pipeline);
    return -1;
  }

  {
    GstBus *bus = NULL;

    // Retrieve reference to the pipeline's bus.
    if ((bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline))) == NULL) {
      g_printerr ("ERROR: Failed to retrieve pipeline bus!\n");

      g_main_loop_unref (mloop);
      gst_object_unref (pipeline);

      return -1;
    }

    // Watch for messages on the pipeline's bus.
    gst_bus_add_signal_watch (bus);

    g_signal_connect (bus, "message::state-changed",
        G_CALLBACK (state_changed_cb), pipeline);
    g_signal_connect (bus, "message::warning", G_CALLBACK (warning_cb), NULL);
    g_signal_connect (bus, "message::error", G_CALLBACK (error_cb), mloop);
    g_signal_connect (bus, "message::eos", G_CALLBACK (eos_cb), mloop);

    gst_object_unref (bus);
  }

  // Connect a callback to the new-sample signal.
  {
    GstElement *element = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
    g_signal_connect (element, "new-sample", G_CALLBACK (new_sample), NULL);
  }

  // Register function for handling interrupt signals with the main loop.
  intrpt_watch_id = g_unix_signal_add (SIGINT, handle_interrupt_signal, pipeline);

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

  // Get instance to qmmfsrc
  GstElement *qtiqmmfsrc = gst_bin_get_by_name (GST_BIN (pipeline), "camera");
  g_signal_connect (qtiqmmfsrc, "result-metadata",
      G_CALLBACK (result_metadata), NULL);

  // Get static metadata
  ::android::CameraMetadata *st_meta_ptr = nullptr;
  g_object_get (G_OBJECT (qtiqmmfsrc), "camera-characteristics",
      &st_meta_ptr, NULL);
  if (st_meta_ptr) {
    g_print ("Get static-metadata entries - %ld\n", st_meta_ptr->entryCount());
    delete st_meta_ptr;
  } else {
    g_printerr ("Get static-metadata failed\n");
  }

  // Get capture metadata
  ::android::CameraMetadata *meta_ptr = nullptr;
  g_object_get (G_OBJECT (qtiqmmfsrc), "capture-metadata", &meta_ptr, NULL);
  if (meta_ptr) {
    g_print ("Get capture-metadata entries - %ld\n", meta_ptr->entryCount());

    // Set auto focus mode metadata
    guchar afmode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
    meta_ptr->update(ANDROID_CONTROL_AF_MODE, &afmode, 1);

    metadata_update.qtiqmmfsrc = qtiqmmfsrc;

    // Release metadata
    delete meta_ptr;
  } else {
    g_printerr ("Get capture-metadata failed\n");
  }

  g_mutex_init (&metadata_update.update_lock);
  g_cond_init (&metadata_update.update_signal);

  // Initiate the metadata update thread.
  GThread *thread = NULL;
  thread = g_thread_new ("metadataUpdateThread",
      metadata_update_thread, &metadata_update);

  // Run main loop.
  g_main_loop_run (mloop);
  g_print ("g_main_loop_run ends\n");

  // Set the finish flag in order to terminate the update thread
  g_mutex_lock (&metadata_update.update_lock);
  metadata_update.finish = 1;
  g_print ("g_main_loop_run STOP the thread\n");
  g_cond_signal (&metadata_update.update_signal);
  g_mutex_unlock (&metadata_update.update_lock);

  g_thread_join (thread);

  g_print ("Setting pipeline to NULL state ...\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_source_remove (intrpt_watch_id);

  g_main_loop_unref (mloop);
  gst_object_unref (pipeline);

  gst_deinit ();

  return 0;
}
