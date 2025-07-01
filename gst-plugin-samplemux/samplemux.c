/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "samplemux.h"

#include <stdio.h>
#include <string.h>

#include <gst/allocators/allocators.h>
#include <gst/video/video.h>

#define GST_CAT_DEFAULT gst_samplemux_debug
GST_DEBUG_CATEGORY (gst_samplemux_debug);

#define gst_samplemux_parent_class parent_class
G_DEFINE_TYPE (GstSampleMux, gst_samplemux, GST_TYPE_ELEMENT);

#define TIMESTAMP_DELTA_THRESHOLD   1000000

#define DEFAULT_PROP_QUEUE_SIZE     10

#define GST_SAMPLEMUX_MEDIA_CAPS  \
    "video/x-raw(ANY)"

#define GST_SAMPLEMUX_SECONDARY_CAPS   \
    "video/x-raw(ANY); "          \
    "video/x-bayer; "             \
    "text/x-raw, format = utf8"

enum
{
  PROP_0,
};

static GstStaticPadTemplate gst_samplemux_media_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_SAMPLEMUX_MEDIA_CAPS)
    );

static GstStaticPadTemplate gst_samplemux_secondary_sink_template =
    GST_STATIC_PAD_TEMPLATE("secondary_%u",
        GST_PAD_SINK,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (GST_SAMPLEMUX_SECONDARY_CAPS)
    );

static GstStaticPadTemplate gst_samplemux_src_template =
    GST_STATIC_PAD_TEMPLATE("src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_SAMPLEMUX_MEDIA_CAPS)
    );


static void
gst_data_queue_free_item (gpointer userdata)
{
  GstDataQueueItem *item = userdata;

  if (item->object != NULL)
    gst_buffer_unref (GST_BUFFER (item->object));

  g_slice_free (GstDataQueueItem, item);
}

static gboolean
gst_caps_is_media_type (const GstCaps * caps, const gchar * mediatype)
{
  GstStructure *s = gst_caps_get_structure (caps, 0);

  return (g_ascii_strcasecmp (gst_structure_get_name (s), mediatype) == 0) ?
      TRUE : FALSE;
}

static GstMetaItem *
gst_metadata_item_new ()
{
  GstMetaItem *item = g_slice_new (GstMetaItem);
  g_return_val_if_fail (item != NULL, NULL);

  item->values = NULL;
  item->timestamp = GST_CLOCK_TIME_NONE;

  return item;
}

static void
gst_metadata_item_free (GstMetaItem * item)
{
  g_list_free_full (item->values, (GDestroyNotify) gst_structure_free);
  g_slice_free (GstMetaItem, item);
}

static gboolean
gst_samplemux_is_meta_available (GstSampleMux * muxer, GstClockTime timestamp)
{
  GList *list = NULL;
  GstMetaItem *item = NULL;
  gboolean available = TRUE, skip = FALSE;

  // Iterate ovr the data pads and check if data available on all of them.
  for (list = muxer->secondarypads; list != NULL; list = g_list_next (list)) {
    GstSampleMuxSecondaryPad *dpad = GST_SAMPLEMUX_SECONDARY_PAD (list->data);
    GstClockTimeDiff delta = GST_CLOCK_TIME_NONE;

    GST_OBJECT_LOCK (dpad);
    skip = GST_PAD_IS_EOS (dpad) || GST_PAD_IS_FLUSHING (dpad);

    // Pads which are in EOS or FLUSHING state are not included in the checks.
    if (skip && g_queue_is_empty (dpad->queue)) {
      GST_OBJECT_UNLOCK (dpad);
      continue;
    }

    GST_OBJECT_UNLOCK (dpad);

    // If there is no data available to at least one pad return immediately.
    if (!(available &= !g_queue_is_empty (dpad->queue)))
      break;

    // If timestamp is not valid, no timestamp matching will be performed.
    if (!GST_CLOCK_TIME_IS_VALID (timestamp))
      continue;

    while ((item = g_queue_peek_head (dpad->queue)) != NULL) {
      // If the item doesn't contain a valid timestamp we cannot do matching.
      if (!GST_CLOCK_TIME_IS_VALID (item->timestamp)) {
        gst_metadata_item_free (g_queue_pop_head (dpad->queue));
        continue;
      }

      delta = GST_CLOCK_DIFF (item->timestamp, timestamp);

      // Timestamp delta is below the threshold, break and continue with next pad.
      if (ABS (delta) <= TIMESTAMP_DELTA_THRESHOLD)
        break;

      // Entry timestamp doesn't match but it's newer, keep it and return immediately.
      if (delta < 0)
        return FALSE;

      // Drop this item as its timestamp is too old.
      gst_metadata_item_free (g_queue_pop_head (dpad->queue));
    }

    // If there is no data left to this pad return immediately.
    if (!(available &= !g_queue_is_empty (dpad->queue)))
      break;
  }

  return available;
}

static void
gst_samplemux_flush_metadata_queues (GstSampleMux * muxer)
{
  GList *list = NULL;

  GST_SAMPLEMUX_LOCK (muxer);

  for (list = muxer->secondarypads; list != NULL; list = g_list_next (list)) {
    GstSampleMuxSecondaryPad *dpad = GST_SAMPLEMUX_SECONDARY_PAD (list->data);

    while (!g_queue_is_empty (dpad->queue))
      gst_metadata_item_free (g_queue_pop_head (dpad->queue));
  }

  g_cond_signal (&(muxer)->wakeup);

  GST_SAMPLEMUX_UNLOCK (muxer);
  return;
}

static gboolean
gst_samplemux_process_meta_entries (GstSampleMux * muxer, GstBuffer * buffer,
    GstClockTime timestamp)
{
  GList *list = NULL;

  // No metadata pads, nothing to process.
  if (muxer->secondarypads == NULL)
    return TRUE;

  for (list = muxer->secondarypads; list != NULL; list = g_list_next (list)) {
    GstSampleMuxSecondaryPad *dpad = GST_SAMPLEMUX_SECONDARY_PAD (list->data);
    GstMetaItem *item = NULL;

    if ((item = g_queue_peek_head (dpad->queue)) == NULL)
      continue;

    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      GstClockTimeDiff delta = GST_CLOCK_DIFF (item->timestamp, timestamp);

      // Timestamp delta is above the threshold, continue with next pad.
      if (ABS (delta) > TIMESTAMP_DELTA_THRESHOLD)
        continue;
    }

    item = g_queue_pop_head (dpad->queue);

    GST_TRACE_OBJECT (dpad, "Processing item with timestamp %" GST_TIME_FORMAT
      " for %" GST_PTR_FORMAT, GST_TIME_ARGS (item->timestamp), buffer);

    // TODO: Users could process the buffer value based on their own requirements.

    gst_metadata_item_free (item);
  }
  return TRUE;
}

static void
gst_samplemux_worker_task (gpointer userdata)
{
  GstSampleMux *muxer = GST_SAMPLEMUX (userdata);
  GstDataQueueItem *item = NULL;
  GstBuffer *buffer = NULL;
  GstClockTime timestamp = GST_CLOCK_TIME_NONE;
  gboolean success = FALSE;

  if (!gst_data_queue_peek (muxer->sinkpad->buffers, &item))
    return;

  // Take the buffer from the queue item and null the object pointer.
  buffer = GST_BUFFER (item->object);
  item->object = NULL;

  GST_TRACE_OBJECT (muxer, "Processing %" GST_PTR_FORMAT, buffer);

  GST_SAMPLEMUX_LOCK (muxer);

  while (muxer->active && !gst_samplemux_is_meta_available (muxer, timestamp))
    g_cond_wait (&(muxer)->wakeup, GST_SAMPLEMUX_GET_LOCK (muxer));

  if (!muxer->active) {
    GST_INFO_OBJECT (muxer, "Task has been deactivated");
    GST_SAMPLEMUX_UNLOCK (muxer);
    goto cleanup;
  }

  // Iterate over all of the data pad queues and extract available data.
  success = gst_samplemux_process_meta_entries (muxer, buffer, timestamp);

  GST_SAMPLEMUX_UNLOCK (muxer);

  if (!success)
    goto cleanup;

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (buffer);
  item->size = gst_buffer_get_size (buffer);
  item->duration = GST_BUFFER_DURATION (buffer);
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  GST_TRACE_OBJECT (muxer, "Submitting %" GST_PTR_FORMAT, buffer);

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (muxer->srcpad->buffers, item))
    item->destroy (item);

cleanup:
  if (!success)
    gst_buffer_unref (buffer);

  // Buffer was sent to srcpad, remove and free the sinkpad item from the queue.
  if (gst_data_queue_pop (muxer->sinkpad->buffers, &item))
    item->destroy (item);
}

static gboolean
gst_samplemux_start_worker_task (GstSampleMux * muxer)
{
  GST_SAMPLEMUX_LOCK (muxer);

  if (muxer->active) {
    GST_SAMPLEMUX_UNLOCK (muxer);
    return TRUE;
  }

  muxer->worktask = gst_task_new (gst_samplemux_worker_task, muxer, NULL);
  gst_task_set_lock (muxer->worktask, &muxer->worklock);

  GST_INFO_OBJECT (muxer, "Created task %p", muxer->worktask);

  muxer->active = TRUE;
  GST_SAMPLEMUX_UNLOCK (muxer);

  if (!gst_task_start (muxer->worktask)) {
    GST_ERROR_OBJECT (muxer, "Failed to start worker task!");
    return FALSE;
  }

  GST_INFO_OBJECT (muxer, "Started task %p", muxer->worktask);
  return TRUE;
}

static gboolean
gst_samplemux_stop_worker_task (GstSampleMux * muxer)
{
  GST_SAMPLEMUX_LOCK (muxer);

  if (!muxer->active) {
    GST_SAMPLEMUX_UNLOCK (muxer);
    return TRUE;
  }

  GST_INFO_OBJECT (muxer, "Stopping task %p", muxer->worktask);

  if (!gst_task_stop (muxer->worktask))
    GST_WARNING_OBJECT (muxer, "Failed to stop worker task!");

  muxer->active = FALSE;
  g_cond_signal (&(muxer)->wakeup);

  GST_SAMPLEMUX_UNLOCK (muxer);

  if (!gst_task_join (muxer->worktask)) {
    GST_ERROR_OBJECT (muxer, "Failed to join worker task!");
    return FALSE;
  }

  GST_INFO_OBJECT (muxer, "Removing task %p", muxer->worktask);

  gst_object_unref (muxer->worktask);

  muxer->worktask = NULL;
  muxer->timeout = GST_CLOCK_TIME_NONE;

  return TRUE;
}

static gboolean
gst_samplemux_parse_nv12_metadata (GstSampleMux * muxer,
    GstSampleMuxSecondaryPad * dpad, GstBuffer * buffer)
{
  // TODO: This function should be rewritten based on user's requirements.
  GstMetaItem *item = NULL;
  item = gst_metadata_item_new ();

  GST_SAMPLEMUX_LOCK (muxer);

  g_queue_push_tail (dpad->queue, item);
  g_cond_signal (&(muxer)->wakeup);

  GST_SAMPLEMUX_UNLOCK (muxer);

  return TRUE;
}

static gboolean
gst_samplemux_parse_raw_metadata (GstSampleMux * muxer,
    GstSampleMuxSecondaryPad * dpad, GstBuffer * buffer)
{
  // TODO: This function should be rewritten based on user's requirements.
  GstMetaItem *item = NULL;
  item = gst_metadata_item_new ();

  GST_SAMPLEMUX_LOCK (muxer);

  g_queue_push_tail (dpad->queue, item);
  g_cond_signal (&(muxer)->wakeup);

  GST_SAMPLEMUX_UNLOCK (muxer);

  return TRUE;
}

static gboolean
gst_samplemux_parse_text_metadata (GstSampleMux * muxer,
    GstSampleMuxSecondaryPad * dpad, GstBuffer * buffer)
{
  // TODO: This function should be rewritten based on user's requirements.
  GstMetaItem *item = NULL;
  item = gst_metadata_item_new ();

  GST_SAMPLEMUX_LOCK (muxer);

  g_queue_push_tail (dpad->queue, item);
  g_cond_signal (&(muxer)->wakeup);

  GST_SAMPLEMUX_UNLOCK (muxer);

  return TRUE;
}

static GstCaps *
gst_samplemux_main_sink_pad_getcaps (GstSampleMux * muxer, GstPad * pad,
    GstCaps * filter)
{
  GstCaps *srccaps = NULL, *templcaps = NULL, *sinkcaps = NULL;

  templcaps = gst_pad_get_pad_template_caps (GST_PAD (muxer->srcpad));

  // Query the source pad peer with the transformed filter.
  srccaps = gst_pad_peer_query_caps (GST_PAD (muxer->srcpad), templcaps);
  gst_caps_unref (templcaps);

  GST_DEBUG_OBJECT (pad, "Src caps %" GST_PTR_FORMAT, srccaps);

  templcaps = gst_pad_get_pad_template_caps (pad);
  sinkcaps = gst_caps_intersect (templcaps, srccaps);

  gst_caps_unref (srccaps);
  gst_caps_unref (templcaps);

  GST_DEBUG_OBJECT (pad, "Filter caps  %" GST_PTR_FORMAT, filter);

  if (filter != NULL) {
    GstCaps *intersection  =
        gst_caps_intersect_full (filter, sinkcaps, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersection);

    gst_caps_unref (sinkcaps);
    sinkcaps = intersection;
  }

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, sinkcaps);
  return sinkcaps;
}

static gboolean
gst_samplemux_main_sink_pad_setcaps (GstSampleMux * muxer, GstPad * pad,
    GstCaps * caps)
{
  GstCaps *srccaps = NULL, *intersect = NULL;

  GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

  // Get the negotiated caps between the srcpad and its peer.
  srccaps = gst_pad_get_allowed_caps (GST_PAD (muxer->srcpad));
  GST_DEBUG_OBJECT (pad, "Source caps %" GST_PTR_FORMAT, srccaps);

  intersect = gst_caps_intersect (srccaps, caps);
  GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersect);

  gst_caps_unref (srccaps);

  if ((intersect == NULL) || gst_caps_is_empty (intersect)) {
    GST_ERROR_OBJECT (pad, "Source and sink caps do not intersect!");

    if (intersect != NULL)
      gst_caps_unref (intersect);

    return FALSE;
  }

  if (gst_pad_has_current_caps (GST_PAD (muxer->srcpad))) {
    srccaps = gst_pad_get_current_caps (GST_PAD (muxer->srcpad));

    if (!gst_caps_is_equal (srccaps, intersect))
      gst_pad_mark_reconfigure (GST_PAD (muxer->srcpad));

    gst_caps_unref (srccaps);
  }

  gst_caps_unref (intersect);

  // Extract video information from caps.
  if (gst_caps_is_media_type (caps, "video/x-raw")) {
    if (muxer->vinfo != NULL)
      gst_video_info_free (muxer->vinfo);

    muxer->vinfo = gst_video_info_new ();

    if (!gst_video_info_from_caps (muxer->vinfo, caps)) {
      GST_ERROR_OBJECT (pad, "Invalid caps %" GST_PTR_FORMAT, caps);
      gst_caps_unref (caps);
      return FALSE;
    }
  }

  GST_DEBUG_OBJECT (pad, "Negotiated caps %" GST_PTR_FORMAT, caps);

  // Wait for pending buffers to be processed before sending new caps.
  GST_SAMPLEMUX_PAD_WAIT_IDLE (GST_SAMPLEMUX_SINK_PAD (pad));
  GST_SAMPLEMUX_PAD_WAIT_IDLE (muxer->srcpad);

  GST_DEBUG_OBJECT (pad, "Pushing new caps %" GST_PTR_FORMAT, caps);
  return gst_pad_push_event (GST_PAD (muxer->srcpad), gst_event_new_caps (caps));
}

static gboolean
gst_samplemux_main_sink_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstSampleMux *muxer = GST_SAMPLEMUX (parent);

  GST_TRACE_OBJECT (muxer, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_event_parse_caps (event, &caps);
      success = gst_samplemux_main_sink_pad_setcaps (muxer, pad, caps);

      gst_event_unref (event);
      return success;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSampleMuxSrcPad *srcpad = muxer->srcpad;
      GstSegment segment;

      gst_event_copy_segment (event, &segment);

      GST_DEBUG_OBJECT (pad, "Got segment: %" GST_SEGMENT_FORMAT, &segment);

      GST_SAMPLEMUX_SRC_LOCK (srcpad);

      if (segment.format == GST_FORMAT_BYTES) {
        gst_segment_init (&(srcpad)->segment, GST_FORMAT_TIME);

        srcpad->segment.start = segment.start;

        GST_DEBUG_OBJECT (pad, "Converted incoming segment to TIME: %"
            GST_SEGMENT_FORMAT, &(srcpad)->segment);
      } else if (segment.format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (pad, "Replacing previous segment: %"
            GST_SEGMENT_FORMAT, &(srcpad)->segment);
        gst_segment_copy_into (&segment, &srcpad->segment);
      } else {
        GST_ERROR_OBJECT (pad, "Unsupported SEGMENT format: %s!",
            gst_format_get_name (segment.format));
        GST_SAMPLEMUX_SRC_UNLOCK (srcpad);
        return FALSE;
      }

      gst_event_unref (event);
      event = gst_event_new_segment (&(srcpad)->segment);

      GST_SAMPLEMUX_SRC_UNLOCK (srcpad);

      return gst_pad_push_event (GST_PAD (srcpad), event);
    }
    case GST_EVENT_FLUSH_START:
      gst_data_queue_set_flushing (GST_SAMPLEMUX_SINK_PAD (pad)->buffers, TRUE);
      gst_data_queue_flush (GST_SAMPLEMUX_SINK_PAD (pad)->buffers);

      gst_samplemux_stop_worker_task (muxer);
      gst_samplemux_flush_metadata_queues (muxer);

      return gst_pad_push_event (pad, event);
    case GST_EVENT_FLUSH_STOP:
      gst_data_queue_set_flushing (GST_SAMPLEMUX_SINK_PAD (pad)->buffers, FALSE);
      gst_samplemux_start_worker_task (muxer);

      return gst_pad_push_event (pad, event);
    case GST_EVENT_EOS:
      GST_SAMPLEMUX_PAD_WAIT_IDLE (GST_SAMPLEMUX_SINK_PAD (pad));
      GST_SAMPLEMUX_PAD_WAIT_IDLE (muxer->srcpad);

      gst_samplemux_flush_metadata_queues (muxer);
      return gst_pad_push_event (GST_PAD (muxer->srcpad), event);
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static gboolean
gst_samplemux_main_sink_pad_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstSampleMux *muxer = GST_SAMPLEMUX (parent);

  GST_TRACE_OBJECT (pad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_samplemux_main_sink_pad_getcaps (muxer, pad, filter);

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);

      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_query_parse_accept_caps (query, &caps);
      GST_DEBUG_OBJECT (pad, "Accept caps: %" GST_PTR_FORMAT, caps);

      if (gst_caps_is_fixed (caps)) {
        GstCaps *tmplcaps = gst_pad_get_pad_template_caps (pad);
        GST_DEBUG_OBJECT (pad, "Template caps: %" GST_PTR_FORMAT, tmplcaps);

        success = gst_caps_can_intersect (tmplcaps, caps);
        gst_caps_unref (tmplcaps);
      }

      gst_query_set_accept_caps_result (query, success);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static GstFlowReturn
gst_samplemux_main_sink_pad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstSampleMuxSinkPad *sinkpad = GST_SAMPLEMUX_SINK_PAD (pad);
  GstSampleMux *muxer = GST_SAMPLEMUX (parent);
  GstDataQueueItem *item = NULL;

  if (!gst_pad_has_current_caps (GST_PAD (muxer->srcpad))) {
    if (GST_PAD_IS_FLUSHING (muxer->srcpad)) {
      gst_buffer_unref (buffer);
      return GST_FLOW_FLUSHING;
    }

    GST_ELEMENT_ERROR (muxer, STREAM, DECODE, ("No caps set!"), (NULL));
    return GST_FLOW_ERROR;
  }

  GST_TRACE_OBJECT (sinkpad, "Received %" GST_PTR_FORMAT, buffer);

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (buffer);
  item->size = gst_buffer_get_size (buffer);
  item->duration = GST_BUFFER_DURATION (buffer);
  item->visible = TRUE;
  item->destroy = gst_data_queue_free_item;

  // Push the buffer into the queue or free it on failure.
  if (!gst_data_queue_push (sinkpad->buffers, item))
    item->destroy (item);

  return GST_FLOW_OK;
}

static gboolean
gst_samplemux_secondary_sink_pad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GST_TRACE_OBJECT (pad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL, *tmplcaps = NULL, *intersect = NULL;

      gst_event_parse_caps (event, &caps);
      gst_event_unref (event);

      GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

      // Get the negotiated caps between the srcpad and its peer.
      tmplcaps = gst_pad_get_pad_template_caps (pad);
      GST_DEBUG_OBJECT (pad, "Template caps %" GST_PTR_FORMAT, tmplcaps);

      intersect = gst_caps_intersect (tmplcaps, caps);
      GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersect);

      gst_caps_unref (tmplcaps);

      if ((intersect == NULL) || gst_caps_is_empty (intersect)) {
        GST_ERROR_OBJECT (pad, "Template and sink caps do not intersect!");

        if (intersect != NULL)
          gst_caps_unref (intersect);

        return FALSE;
      }

      if (gst_caps_is_media_type (caps, "video/x-raw"))
        GST_SAMPLEMUX_SECONDARY_PAD (pad)->type = GST_SECONDARY_TYPE_NV12;
      else if (gst_caps_is_media_type (caps, "video/x-bayer"))
        GST_SAMPLEMUX_SECONDARY_PAD (pad)->type = GST_SECONDARY_TYPE_RAW;
      else if (gst_caps_is_media_type (caps, "text/x-raw"))
        GST_SAMPLEMUX_SECONDARY_PAD (pad)->type = GST_SECONDARY_TYPE_TEXT;
      else
        GST_SAMPLEMUX_SECONDARY_PAD (pad)->type = GST_SECONDARY_TYPE_UNKNOWN;

      return TRUE;
    }
    case GST_EVENT_FLUSH_START:
    {
      GstSampleMux *muxer = GST_SAMPLEMUX (parent);

      GST_SAMPLEMUX_LOCK (muxer);
      // Flushing flag has been already set, just notify the worker task.
      g_cond_signal (&(muxer)->wakeup);
      GST_SAMPLEMUX_UNLOCK (muxer);

      gst_event_unref (event);
      return TRUE;
    }
    case GST_EVENT_EOS:
      gst_event_unref (event);
      return TRUE;
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_SEGMENT:
    case GST_EVENT_GAP:
    case GST_EVENT_STREAM_START:
      // Drop the event, those events are forwarded by the main sink pad.
      gst_event_unref (event);
      return TRUE;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static GstFlowReturn
gst_samplemux_secondary_sink_pad_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstSampleMux *muxer = GST_SAMPLEMUX (parent);
  GstSampleMuxSecondaryPad *dpad = GST_SAMPLEMUX_SECONDARY_PAD (pad);
  GstClockTime time = GST_CLOCK_TIME_NONE;
  gboolean success = FALSE;

  if (GST_PAD_IS_FLUSHING (muxer->srcpad)) {
    gst_buffer_unref (buffer);
    return GST_FLOW_FLUSHING;
  }

  // If the main sink pad has reached EOS return EOS for data(meta) pads.
  if (GST_PAD_IS_EOS (muxer->sinkpad)) {
    gst_buffer_unref (buffer);
    return GST_FLOW_EOS;
  }

  if (gst_buffer_get_size (buffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP)) {
    GstMetaItem *item = gst_metadata_item_new ();

    // Create an empty item with the buffer TS for synchronization purpose.
    item->timestamp = GST_BUFFER_TIMESTAMP (buffer);

    GST_SAMPLEMUX_LOCK (muxer);

    g_queue_push_tail (dpad->queue, item);
    g_cond_signal (&(muxer)->wakeup);

    GST_SAMPLEMUX_UNLOCK (muxer);

    // Buffer is marked as GAP, nothing to process. Just consume it.
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  GST_TRACE_OBJECT (pad, "Received %" GST_PTR_FORMAT, buffer);

  time = gst_util_get_timestamp ();

  if (dpad->type == GST_SECONDARY_TYPE_NV12)
    success = gst_samplemux_parse_nv12_metadata (muxer, dpad, buffer);
  else if (dpad->type == GST_SECONDARY_TYPE_RAW)
    success = gst_samplemux_parse_raw_metadata (muxer, dpad, buffer);
  else if (dpad->type == GST_SECONDARY_TYPE_TEXT)
    success = gst_samplemux_parse_text_metadata (muxer, dpad, buffer);

  time = GST_CLOCK_DIFF (time, gst_util_get_timestamp ());

  GST_LOG_OBJECT (pad, "Parse took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (time),
      (GST_TIME_AS_USECONDS (time) % 1000));

  gst_buffer_unref (buffer);
  return success ? GST_FLOW_OK : GST_FLOW_ERROR;
}

static GstPad*
gst_samplemux_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstSampleMux *muxer = GST_SAMPLEMUX (element);
  GstPad *pad = NULL;
  gchar *name = NULL;
  guint index = 0, nextindex = 0;

  GST_SAMPLEMUX_LOCK (muxer);

  if (reqname && sscanf (reqname, "secondary_%u", &index) == 1) {
    // Update the next sink pad index set his name.
    nextindex = (index >= muxer->nextidx) ? index + 1 : muxer->nextidx;
  } else {
    index = muxer->nextidx;
    // Update the index for next video pad and set his name.
    nextindex = index + 1;
  }

  name = g_strdup_printf ("secondary_%u", index);

  pad = g_object_new (GST_TYPE_SAMPLEMUX_SECONDARY_PAD, "name", name, "direction",
      templ->direction, "template", templ, NULL);
  g_free (name);

  if (pad == NULL) {
    GST_ERROR_OBJECT (muxer, "Failed to create sink pad!");
    return NULL;
  }

  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_samplemux_secondary_sink_pad_event));
  gst_pad_set_chain_function (pad,
      GST_DEBUG_FUNCPTR (gst_samplemux_secondary_sink_pad_chain));

  if (!gst_element_add_pad (element, pad)) {
    GST_ERROR_OBJECT (muxer, "Failed to add sink pad!");
    gst_object_unref (pad);
    return NULL;
  }

  muxer->secondarypads = g_list_append (muxer->secondarypads, pad);
  muxer->nextidx = nextindex;

  GST_SAMPLEMUX_UNLOCK (muxer);

  GST_DEBUG_OBJECT (muxer, "Created pad: %s", GST_PAD_NAME (pad));
  return pad;
}

static void
gst_samplemux_release_pad (GstElement * element, GstPad * pad)
{
  GstSampleMux *muxer = GST_SAMPLEMUX (element);

  GST_DEBUG_OBJECT (muxer, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_SAMPLEMUX_LOCK (muxer);
  muxer->secondarypads = g_list_remove (muxer->secondarypads, pad);
  GST_SAMPLEMUX_UNLOCK (muxer);

  gst_element_remove_pad (element, pad);
}

static GstStateChangeReturn
gst_samplemux_change_state (GstElement * element, GstStateChange transition)
{
  GstSampleMux *muxer = GST_SAMPLEMUX (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_data_queue_set_flushing (muxer->sinkpad->buffers, FALSE);
      gst_samplemux_start_worker_task (muxer);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      // FLush buffers otherwise the chain function may get blocked if the queue
      // is full and this will lead to a deadlock with the pad deactivation
      // calls during change_state() below as we will be holding STREAM_LOCK.
      gst_data_queue_set_flushing (muxer->sinkpad->buffers, TRUE);
      gst_data_queue_flush (muxer->sinkpad->buffers);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_samplemux_stop_worker_task (muxer);
      gst_samplemux_flush_metadata_queues (muxer);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_samplemux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_samplemux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_samplemux_finalize (GObject * object)
{
  GstSampleMux *muxer = GST_SAMPLEMUX (object);

  if (muxer->vinfo != NULL)
    gst_video_info_free (muxer->vinfo);

  g_rec_mutex_clear (&muxer->worklock);
  g_cond_clear (&muxer->wakeup);

  g_mutex_clear (&muxer->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (muxer));
}

static void
gst_samplemux_class_init (GstSampleMuxClass *klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  object->set_property = GST_DEBUG_FUNCPTR (gst_samplemux_set_property);
  object->get_property = GST_DEBUG_FUNCPTR (gst_samplemux_get_property);
  object->finalize     = GST_DEBUG_FUNCPTR (gst_samplemux_finalize);

  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_samplemux_secondary_sink_template, GST_TYPE_SAMPLEMUX_SECONDARY_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_samplemux_src_template, GST_TYPE_SAMPLEMUX_SRC_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
    &gst_samplemux_media_sink_template, GST_TYPE_SAMPLEMUX_SINK_PAD);

  gst_element_class_set_static_metadata (element,
      "Sample muxer", "Video/RAW/Text/Muxer",
      "Muxes data stream as GstMeta with video, raw or text stream", "QTI"
  );

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_samplemux_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_samplemux_release_pad);
  element->change_state = GST_DEBUG_FUNCPTR (gst_samplemux_change_state);

  // Initializes a new muxer GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (gst_samplemux_debug, "qtisamplemux", 0, "QTI Sample Muxer");
}

static void
gst_samplemux_init (GstSampleMux * muxer)
{
  GstPadTemplate *template = NULL;

  g_mutex_init (&muxer->lock);

  muxer->nextidx = 0;
  muxer->secondarypads = NULL;

  muxer->vinfo = NULL;

  muxer->active = FALSE;
  muxer->worktask = NULL;
  muxer->timeout = GST_CLOCK_TIME_NONE;

  g_rec_mutex_init (&muxer->worklock);
  g_cond_init (&muxer->wakeup);

  muxer->queue_size = DEFAULT_PROP_QUEUE_SIZE;

  template = gst_static_pad_template_get (&gst_samplemux_media_sink_template);
  muxer->sinkpad = g_object_new (GST_TYPE_SAMPLEMUX_SINK_PAD, "name", "sink",
      "direction", template->direction, "template", template, NULL);
  gst_object_unref (template);

  gst_pad_set_event_function (GST_PAD (muxer->sinkpad),
      GST_DEBUG_FUNCPTR (gst_samplemux_main_sink_pad_event));
  gst_pad_set_query_function (GST_PAD (muxer->sinkpad),
      GST_DEBUG_FUNCPTR (gst_samplemux_main_sink_pad_query));
  gst_pad_set_chain_function (GST_PAD (muxer->sinkpad),
      GST_DEBUG_FUNCPTR (gst_samplemux_main_sink_pad_chain));

  GST_OBJECT_FLAG_SET (muxer->sinkpad, GST_PAD_FLAG_PROXY_ALLOCATION);

  gst_element_add_pad (GST_ELEMENT (muxer), GST_PAD (muxer->sinkpad));
  muxer->sinkpad->buffers_limit = muxer->queue_size;

  template = gst_static_pad_template_get (&gst_samplemux_src_template);
  muxer->srcpad = g_object_new (GST_TYPE_SAMPLEMUX_SRC_PAD, "name", "src",
      "direction", template->direction, "template", template, NULL);
  gst_object_unref (template);

  gst_pad_set_event_function (GST_PAD (muxer->srcpad),
      GST_DEBUG_FUNCPTR (gst_samplemux_src_pad_event));
  gst_pad_set_query_function (GST_PAD (muxer->srcpad),
      GST_DEBUG_FUNCPTR (gst_samplemux_src_pad_query));
  gst_pad_set_activatemode_function (GST_PAD (muxer->srcpad),
      GST_DEBUG_FUNCPTR (gst_samplemux_src_pad_activate_mode));
  gst_element_add_pad (GST_ELEMENT (muxer), GST_PAD (muxer->srcpad));
  muxer->srcpad->buffers_limit = muxer->queue_size;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtisamplemux", GST_RANK_NONE,
      GST_TYPE_SAMPLEMUX);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtisamplemux,
    "QTI Sample Muxer",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
