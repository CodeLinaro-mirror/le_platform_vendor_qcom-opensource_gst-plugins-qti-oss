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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "triton.h"

#include <stdio.h>
#include <time.h>
#include <gst/video/gstimagepool.h>

#include "tritonpads.h"
#include "tritoninfer.h"


#define GST_CAT_DEFAULT gst_triton_debug
GST_DEBUG_CATEGORY_STATIC (gst_triton_debug);

#define gst_triton_parent_class parent_class
G_DEFINE_TYPE (GstTriton, gst_triton, GST_TYPE_ELEMENT);

GST_DEFINE_MINI_OBJECT_TYPE (GstTritonRequest, gst_triton_request);

#define DEFAULT_PROP_TRITON_MODE GRPC_MODE
#define DEFAULT_PROP_TRITON_TASK DETECTION
#define DEFAULT_PROP_URL NULL
#define DEFAULT_PROP_MODEL_NAME NULL
#define DEFAULT_PROP_MODEL_VERSION NULL
#define DEFAULT_PROP_LABELS NULL
#define DEFAULT_PROP_THRESHOLD 10.0F
#define DEFAULT_PROP_KEEP_RATIO TRUE

#define START_REQUEST_COUNT 1
#define MAX_REQUEST_COUNT 100
#define MIN_REQUEST_COUNT 50
#define GST_BINARY_8BIT_FORMAT "%c%c%c%c%c%c%c%c"
#define GST_BINARY_8BIT_STRING(x) \
  (x & 0x80 ? '1' : '0'), (x & 0x40 ? '1' : '0'), (x & 0x20 ? '1' : '0'), \
  (x & 0x10 ? '1' : '0'), (x & 0x08 ? '1' : '0'), (x & 0x04 ? '1' : '0'), \
  (x & 0x02 ? '1' : '0'), (x & 0x01 ? '1' : '0')

#define GST_PROTECTION_META_CAST(obj) ((GstProtectionMeta *) obj)

#define GST_TRITON_TENSOR_TYPES \
    "{ RGB }"

#define GST_TRITON_SINK_CAPS                   \
    "video/x-raw, "                            \
    "format = (string) " GST_TRITON_TENSOR_TYPES

#define GST_TRITON_SRC_CAPS                    \
    "video/x-raw, "                            \
    "format = (string) " GST_TRITON_TENSOR_TYPES

enum
{
  PROP_0,
  PROP_TRITON_MODE,
  PROP_TRITON_TASK,
  PROP_URL,
  PROP_MODEL_NAME,
  PROP_MODEL_VERSION,
  PROP_LABELS,
  PROP_THRESHOLD,
  PROP_KEEP_RATIO,
};

static GstStaticPadTemplate gst_triton_sink_template =
    GST_STATIC_PAD_TEMPLATE("sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS (GST_TRITON_SINK_CAPS)
    );

static GstStaticPadTemplate gst_triton_src_template =
    GST_STATIC_PAD_TEMPLATE("src_%u",
        GST_PAD_SRC,
        GST_PAD_REQUEST,
        GST_STATIC_CAPS (GST_TRITON_SRC_CAPS)
    );

GType
gst_triton_mode_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue variants[] = {
    { HTTP_MODE,
        "Communicate with Triton Server using http", "http"
    },
    { GRPC_MODE,
        "Communicate with Triton Server using gRPC.", "gRPC"
    },
    { C_API_MODE,
        "Using C API to do inference in Triton Server", "cAPI"
    },
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstTritonMode", variants);

  return gtype;
}

GType
gst_triton_task_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue variants[] = {
    { DETECTION,
        "Object Detection", "detection"
    },
    { CLASSIFICATION,
        "Classification", "classification"
    },
    { SEGMENTATION,
        "Segmentation", "segmentation"
    },
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstTritonTask", variants);

  return gtype;
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

static void
gst_data_queue_free_item (gpointer userdata)
{
  GstDataQueueItem *item = userdata;
  gst_buffer_unref (GST_BUFFER (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static void
gst_triton_request_free (GstTritonRequest * request)
{
  GstBuffer *buffer = NULL;

  buffer = request->inbuffer;
  if (buffer != NULL) {
    gst_buffer_unref (buffer);
  }
  delete_result (request->result);
  g_list_foreach (request->outputs, (GFunc)gst_free_output, NULL);
  g_list_free (request->outputs);
  g_free (request);
}

static GstTritonRequest *
gst_triton_request_new ()
{
  GstTritonRequest *request = g_new0 (GstTritonRequest, 1);

  gst_mini_object_init (GST_MINI_OBJECT (request), 0,
      GST_TYPE_TRITON_REQUEST, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_triton_request_free);

  request->done = FALSE;
  request->id = 0;
  request->result = NULL;
  request->outputs = NULL;
  request->inbuffer = NULL;

  return request;
}

static inline void
gst_triton_request_unref (GstTritonRequest * request)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (request));
}

static void
gst_request_free_queue_item (gpointer data)
{
  GstDataQueueItem *item = data;
  gst_triton_request_unref (GST_TRITON_REQUEST (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static gboolean
gst_triton_all_src_pads_empty (GstTriton * triton, GstEvent *event)
{
  GList *list = NULL;
  gboolean result = TRUE;
  GstTritonSrcPad *srcpad;
  for (list = triton->srcpads; list != NULL; list = g_list_next (list)) {
    srcpad = GST_TRITON_SRCPAD (list->data);
    if (!srcpad->eos_flag) {
      if (gst_data_queue_is_empty (srcpad->buffers) && !srcpad->pushing_buffer) {
        if (gst_pad_push_event (GST_PAD (srcpad), gst_event_ref (event))) {
          srcpad->eos_flag = TRUE;
        } else {
          GST_ERROR_OBJECT (srcpad, "Triton source pad push event %s fail !",
            GST_EVENT_TYPE_NAME (event));
        }
      } else {
        result = FALSE;
      }
    }
  }
  return result;
}

static GstProtectionMeta *
gst_buffer_get_protection_meta_id (GstBuffer * buffer, const gchar * name)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta_filtered (buffer, &state,
              GST_PROTECTION_META_API_TYPE))) {
    if (gst_structure_has_name (GST_PROTECTION_META_CAST (meta)->info, name))
      return GST_PROTECTION_META_CAST (meta);
  }

  return NULL;
}

static gboolean
gst_triton_src_pad_push_event (GstElement * element, GstPad * pad,
    gpointer userdata)
{
  GstTriton *triton = GST_TRITON (element);
  GstEvent *event = GST_EVENT (userdata);

  GST_TRACE_OBJECT (triton, "Event: %s", GST_EVENT_TYPE_NAME (event));
  return gst_pad_push_event (pad, gst_event_ref (event));
}

static GstCaps *
gst_triton_sink_getcaps (GstPad * pad, GstCaps * filter)
{
  GstCaps *caps = NULL, *intersect = NULL;

  if (!(caps = gst_pad_get_current_caps (pad)))
    caps = gst_pad_get_pad_template_caps (pad);

  GST_DEBUG_OBJECT (pad, "Current caps: %" GST_PTR_FORMAT, caps);

  if (filter != NULL) {
    GST_DEBUG_OBJECT (pad, "Filter caps: %" GST_PTR_FORMAT, caps);
    intersect = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (caps);
    caps = intersect;
  }

  GST_DEBUG_OBJECT (pad, "Returning caps: %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_triton_sink_acceptcaps (GstPad * pad, GstCaps * caps)
{
  GstCaps *tmplcaps = NULL;
  gboolean success = TRUE;

  GST_DEBUG_OBJECT (pad, "Caps %" GST_PTR_FORMAT, caps);

  tmplcaps = gst_pad_get_pad_template_caps (GST_PAD (pad));
  GST_DEBUG_OBJECT (pad, "Template: %" GST_PTR_FORMAT, tmplcaps);

  success &= gst_caps_can_intersect (caps, tmplcaps);
  gst_caps_unref (tmplcaps);

  if (!success) {
    GST_WARNING_OBJECT (pad, "Caps can't intersect with template!");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_triton_sink_setcaps (GstTriton * triton, GstPad * pad, GstCaps * caps)
{
  GstCaps *srccaps = NULL, *intersect = NULL;
  GList *list = NULL;
  GstVideoInfo video_info;

  GST_DEBUG_OBJECT (pad, "Setting caps %" GST_PTR_FORMAT, caps);

  GST_TRITON_LOCK (triton);

  if (!gst_video_info_from_caps (&video_info, caps)) {
    GST_ERROR_OBJECT (triton, "Failed to get input video info from caps %"
        GST_PTR_FORMAT "!", caps);
    return FALSE;
  }
  triton->src_height = video_info.height;
  triton->src_width = video_info.width;

  for (list = triton->srcpads; list != NULL; list = g_list_next (list)) {
    GstTritonSrcPad *srcpad = GST_TRITON_SRCPAD (list->data);

    // Get the negotiated caps between the srcpad and its peer.
    srccaps = gst_pad_get_allowed_caps (GST_PAD (srcpad));
    GST_DEBUG_OBJECT (pad, "Source caps %" GST_PTR_FORMAT, srccaps);

    intersect = gst_caps_intersect (srccaps, caps);
    GST_DEBUG_OBJECT (pad, "Intersected caps %" GST_PTR_FORMAT, intersect);

    gst_caps_unref (srccaps);

    if ((intersect == NULL) || gst_caps_is_empty (intersect)) {
      GST_ELEMENT_ERROR (triton, CORE, NEGOTIATION, (NULL),
          ("Source %s and sink caps do not intersect!", GST_PAD_NAME (srcpad)));

      if (intersect != NULL)
        gst_caps_unref (intersect);

      GST_TRITON_UNLOCK (triton);
      return FALSE;
    }

    if (!gst_pad_set_caps (GST_PAD (srcpad), intersect)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (triton), CORE, NEGOTIATION, (NULL),
          ("Failed to set caps to %s!", GST_PAD_NAME (srcpad)));

      GST_TRITON_UNLOCK (triton);
      return FALSE;
    }
    gst_caps_unref (intersect);
  }

  GST_TRITON_UNLOCK (triton);
  return TRUE;
}

static void
gst_triton_wait_request_task (gpointer userdata)
{
  GstTriton *triton = GST_TRITON (userdata);
  GList *list = NULL;
  GstBuffer *inbuffer = NULL;
  GstBuffer *outbuffer = NULL;
  GstDataQueueItem *item = NULL;
  GstTritonRequest *request = NULL;
  GstProtectionMeta *pmeta = NULL;
  gchar *name = NULL;
  guint channel = 0, n_memory = 0, mem_indx = 0;

  GST_TRITON_LOCK (triton);
  if (gst_data_queue_is_empty (triton->requests)) {
    g_cond_wait (&triton->wakeup, GST_TRITON_GET_LOCK (triton));
    GST_TRITON_UNLOCK (triton);
    return;
  }
  GST_TRITON_UNLOCK (triton);

  if (gst_data_queue_pop (triton->requests, &item)) {
    request = GST_TRITON_REQUEST (gst_mini_object_ref (item->object));
    item->destroy (item);
    inbuffer = request->inbuffer;
    n_memory = gst_buffer_n_memory (inbuffer);

    while (TRUE) {
      if (request->done == TRUE) {
        break;
      } else {
        g_usleep (30000);
      }
    }
    triton_parse_output (userdata, request);
    for (list = triton->srcpads; list != NULL; list = g_list_next (list)) {
      GstTritonSrcPad *srcpad = GST_TRITON_SRCPAD (list->data);
      GstClockTime timestamp = GST_CLOCK_TIME_NONE, duration = GST_CLOCK_TIME_NONE;
      guint flags = 0;

      if (GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP)) {
        GST_ERROR_OBJECT (triton, "Incompatible number of memory blocks (%u) and ", n_memory);
        continue;
      }

      channel = g_list_index (triton->srcpads, srcpad);

      // Check if a inference was done for this channel.
      if ((GST_BUFFER_OFFSET (inbuffer) & (1 << channel)) == 0)
        continue;

      // Create a new buffer wrapper to hold a reference to input buffer.
      outbuffer = gst_buffer_new ();

      name = g_strdup_printf ("channel-%u", channel);

      // Transfer the proper GstProtectionMeta into the new buffer if available.
      if ((pmeta = gst_buffer_get_protection_meta_id (inbuffer, name)) != NULL)
        pmeta = gst_buffer_add_protection_meta (outbuffer,
            gst_structure_copy (pmeta->info));

      g_free (name);

      if ((pmeta != NULL) && gst_structure_has_field (pmeta->info, "timestamp")) {
        gst_structure_get_uint64 (pmeta->info, "timestamp", &timestamp);
        gst_structure_remove_field (pmeta->info, "timestamp");
      }

      if ((pmeta != NULL) && gst_structure_has_field (pmeta->info, "duration")) {
        gst_structure_get_uint64 (pmeta->info, "duration", &duration);
        gst_structure_remove_field (pmeta->info, "duration");
      }

      if ((pmeta != NULL) && gst_structure_has_field (pmeta->info, "flags")) {
        gst_structure_get_uint (pmeta->info, "flags", &flags);
        gst_structure_remove_field (pmeta->info, "flags");
      } else {
        flags = GST_BUFFER_FLAGS (inbuffer);
      }

      GST_BUFFER_TIMESTAMP (outbuffer) = (timestamp != GST_CLOCK_TIME_NONE) ?
          timestamp : GST_BUFFER_TIMESTAMP (inbuffer);
      GST_BUFFER_DURATION (outbuffer) = (duration != GST_CLOCK_TIME_NONE) ?
          duration : GST_BUFFER_DURATION (inbuffer);

      gst_buffer_set_flags (outbuffer, flags);
      gst_buffer_append_memory (outbuffer, gst_buffer_get_memory (inbuffer, mem_indx));

      // Initialize and send the source segment for synchronization.
      if (GST_FORMAT_UNDEFINED == srcpad->segment.format) {
        gst_segment_init (&(srcpad)->segment, GST_FORMAT_TIME);

        srcpad->segment.start = GST_BUFFER_TIMESTAMP (outbuffer);
        srcpad->segment.position = GST_BUFFER_TIMESTAMP (outbuffer);

        gst_pad_push_event (GST_PAD (srcpad),
            gst_event_new_segment (&(srcpad)->segment));
      }

      // Adjust the source pad segment position.
      srcpad->segment.position = GST_BUFFER_TIMESTAMP (outbuffer) +
          GST_BUFFER_DURATION (outbuffer);

      GstMapInfo mapinfo;
      gst_buffer_map (outbuffer, &mapinfo, GST_MAP_READWRITE);
      draw_result (userdata, request, &mapinfo, channel);
      gst_buffer_unmap (outbuffer, &mapinfo);

      item = g_slice_new0 (GstDataQueueItem);
      item->object = GST_MINI_OBJECT (outbuffer);
      item->size = gst_buffer_get_size (outbuffer);
      item->duration = GST_BUFFER_DURATION (outbuffer);
      item->visible = TRUE;
      item->destroy = gst_data_queue_free_item;

      // Push the buffer into the queue or free it on failure.
      if (!gst_data_queue_push (srcpad->buffers, item)) {
        item->destroy (item);
      } else {
      }
      mem_indx ++;
    }

    gst_triton_request_unref (request);

    gst_data_queue_get_level (triton->requests, triton->queue_size);
    if (triton->queue_size->visible < MIN_REQUEST_COUNT) {
      GST_TRITON_LOCK (triton);
      g_cond_signal (GST_TRITON_REQUESTS_COND (triton));
      GST_TRITON_UNLOCK (triton);
    }

    if (gst_data_queue_is_empty (triton->requests)) {
      GST_TRITON_LOCK (triton);
      g_cond_signal (&triton->queue_is_empty);
      GST_TRITON_UNLOCK (triton);
    }
  } else {
    GST_DEBUG_OBJECT (triton, "Paused worker thread");
  }
}

static gboolean
gst_triton_start_worker_task (GstTriton * triton)
{
  if (triton->worktask != NULL)
    return TRUE;

  triton->worktask = gst_task_new (gst_triton_wait_request_task, triton, NULL);
  gst_task_set_lock (triton->worktask, &triton->worklock);

  GST_INFO_OBJECT (triton, "Created task %p", triton->worktask);

  GST_TRITON_LOCK (triton);
  triton->active = TRUE;
  GST_TRITON_UNLOCK (triton);

  if (!gst_task_start (triton->worktask)) {
    GST_ERROR_OBJECT (triton, "Failed to start worker task!");
    return FALSE;
  }

  GST_INFO_OBJECT (triton, "Started task %p", triton->worktask);
  return TRUE;
}

static gboolean
gst_triton_stop_worker_task (GstTriton * triton)
{
  if (NULL == triton->worktask)
    return TRUE;

  GST_INFO_OBJECT (triton, "Stopping task %p", triton->worktask);

  if (!gst_task_stop (triton->worktask)) {
    GST_WARNING_OBJECT (triton, "Failed to stop worker task!");
  }

  GST_TRITON_LOCK (triton);

  triton->active = FALSE;
  g_cond_signal (&(triton)->wakeup);

  GST_TRITON_UNLOCK (triton);

  // Make sure task is not running.
  g_rec_mutex_lock (&triton->worklock);
  g_rec_mutex_unlock (&triton->worklock);


  if (!gst_task_join (triton->worktask)) {
    GST_ERROR_OBJECT (triton, "Failed to join worker task!");
    return FALSE;
  }

  GST_INFO_OBJECT (triton, "Removing task %p", triton->worktask);

  gst_object_unref (triton->worktask);
  triton->worktask = NULL;

  return TRUE;
}

static void
gst_triton_src_pad_worker_task (gpointer userdata)
{
  GstTritonSrcPad *srcpad = GST_TRITON_SRCPAD (userdata);
  GstTriton *triton = GST_TRITON (srcpad->object);
  GstDataQueueItem *item = NULL;

  if (!triton->active && gst_data_queue_is_empty (srcpad->buffers)) {
    GST_TRITON_LOCK (triton);
    g_cond_signal (&(triton)->queue_is_empty);
    GST_TRITON_UNLOCK (triton);
  }

  if (gst_data_queue_pop (srcpad->buffers, &item)) {
    GstBuffer *buffer = gst_buffer_ref (GST_BUFFER (item->object));
    item->destroy (item);

    GST_TRACE_OBJECT (srcpad, "Submitting %" GST_PTR_FORMAT, buffer);
    srcpad->pushing_buffer = TRUE;
    gst_pad_push (GST_PAD (srcpad), buffer);
    srcpad->pushing_buffer = FALSE;
  } else {
    GST_INFO_OBJECT (srcpad, "Pause worker task!");
    gst_pad_pause_task (GST_PAD (srcpad));
  }
}

static GstFlowReturn
gst_triton_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * inbuffer)
{
  GstTriton *triton = GST_TRITON (parent);
  GstDataQueueItem *item = NULL;
  GstTritonRequest *request = NULL;
  GstMemory *memory = NULL;
  GstMapInfo mapinfo;
  guint num = 0, n_memory = 0, size = 0, batch_size = 0, idx = 0;
  static guint id = 0;

  batch_size = g_list_length (triton->srcpads);
  n_memory = gst_buffer_n_memory (inbuffer);
  size = gst_buffer_get_size (inbuffer);

  GST_TRACE_OBJECT (pad, "Received buffer %p of size %u with %u memory blocks,"
      " channels mask " GST_BINARY_8BIT_FORMAT ", timestamp %" GST_TIME_FORMAT
      ", duration %" GST_TIME_FORMAT " flags 0x%X", inbuffer, size, n_memory,
      GST_BINARY_8BIT_STRING (GST_BUFFER_OFFSET (inbuffer)),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (inbuffer)),
      GST_BUFFER_FLAGS (inbuffer));

  GST_TRITON_LOCK (triton);

  gst_data_queue_get_level (triton->requests, triton->queue_size);
  if (triton->queue_size->visible >= MAX_REQUEST_COUNT)
    g_cond_wait(GST_TRITON_REQUESTS_COND (triton), GST_TRITON_GET_LOCK (triton));

  // Create new triton request.
  request = gst_triton_request_new ();
  request->inbuffer = inbuffer;
  request->id = id;

  for (idx = 0; idx < batch_size; idx++) {
    if ((GST_BUFFER_OFFSET (inbuffer) & (1 << idx)) == 0)
      continue;
    memory = gst_buffer_get_memory (inbuffer, num);
    gst_memory_map (memory, &mapinfo, GST_MAP_READ);
    frame_to_inputbuf (&mapinfo, parent, idx);
    gst_memory_unmap (memory, &mapinfo);
    gst_memory_unref (memory);
    num++;
  }
  triton_infer (parent, request);

  // push output to queue
  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (request);
  item->visible = TRUE;
  item->destroy = gst_request_free_queue_item;

  if (!gst_data_queue_push (triton->requests, item))
    item->destroy (item);
  id ++;

  gst_data_queue_get_level (triton->requests, triton->queue_size);
  if (triton->queue_size->visible == START_REQUEST_COUNT)
    g_cond_signal (&(triton)->wakeup);
  GST_TRITON_UNLOCK (triton);

  return GST_FLOW_OK;
}

static gboolean
gst_triton_sink_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GST_TRACE_OBJECT (pad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_triton_sink_getcaps (pad, filter);

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);

      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_query_parse_accept_caps (query, &caps);
      success = gst_triton_sink_acceptcaps (pad, caps);

      gst_query_set_accept_caps_result (query, success);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

static gboolean
gst_triton_sink_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstTriton *triton = GST_TRITON (parent);
  gboolean success = FALSE;

  GST_TRACE_OBJECT (pad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;

      gst_event_parse_caps (event, &caps);
      success = gst_triton_sink_setcaps (triton, pad, caps);
      gst_event_unref (event);

      return success;
    }
    case GST_EVENT_SEGMENT:
    {
      GstTritonSinkPad *sinkpad = GST_TRITON_SINKPAD (pad);
      GstSegment segment;

      gst_event_copy_segment (event, &segment);
      GST_DEBUG_OBJECT (pad, "Got segment: %" GST_SEGMENT_FORMAT, &segment);

      if (segment.format == GST_FORMAT_BYTES) {
        gst_segment_init (&(sinkpad)->segment, GST_FORMAT_TIME);
        sinkpad->segment.start = segment.start;

        GST_DEBUG_OBJECT (pad, "Converted incoming segment to TIME: %"
            GST_SEGMENT_FORMAT, &(sinkpad)->segment);
      } else if (segment.format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (pad, "Replacing previous segment: %"
            GST_SEGMENT_FORMAT, &(sinkpad)->segment);
        gst_segment_copy_into (&segment, &(sinkpad)->segment);
      } else {
        GST_ERROR_OBJECT (pad, "Unsupported SEGMENT format: %s!",
            gst_format_get_name (segment.format));
        return FALSE;
      }
      gst_event_unref (event);

      return TRUE;
    }
    case GST_EVENT_STREAM_START:
      success = gst_element_foreach_src_pad (GST_ELEMENT (triton),
          gst_triton_src_pad_push_event, event);
      gst_event_unref (event);
      return success;
    case GST_EVENT_FLUSH_START:
      success = gst_element_foreach_src_pad (GST_ELEMENT (triton),
          gst_triton_src_pad_push_event, event);
      gst_triton_stop_worker_task (triton);
      gst_event_unref (event);
      return success;
    case GST_EVENT_FLUSH_STOP:
    {
      GstTritonSinkPad *sinkpad = GST_TRITON_SINKPAD (pad);
      GList *list = NULL;

      GST_TRITON_UNLOCK (triton);

      for (list = GST_ELEMENT (triton)->srcpads; list; list = list->next) {
        GstTritonSrcPad *srcpad = GST_TRITON_SRCPAD (list->data);
        gst_segment_init (&(srcpad)->segment, GST_FORMAT_TIME);
      }

      GST_TRITON_UNLOCK (triton);

      gst_segment_init (&(sinkpad)->segment, GST_FORMAT_UNDEFINED);

      success = gst_element_foreach_src_pad (GST_ELEMENT (triton),
          gst_triton_src_pad_push_event, event);
      gst_triton_stop_worker_task (triton);
      gst_event_unref (event);
      return success;
    }
    case GST_EVENT_EOS:
      GST_TRITON_LOCK (triton);
      while (!gst_data_queue_is_empty (triton->requests)) {
        g_cond_wait (&triton->queue_is_empty, GST_TRITON_GET_LOCK (triton));
      }
      GST_TRITON_UNLOCK (triton);
      gst_triton_stop_worker_task (triton);

      GST_TRITON_LOCK (triton);
      while (!gst_triton_all_src_pads_empty (triton, event)) {
        g_cond_wait (&triton->queue_is_empty, GST_TRITON_GET_LOCK (triton));
      }
      GST_TRITON_UNLOCK (triton);

      gst_event_unref (event);
      return TRUE;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}


gboolean
gst_triton_src_pad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstTritonSrcPad *srcpad = GST_TRITON_SRCPAD (pad);

  GST_TRACE_OBJECT (srcpad, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  return gst_pad_event_default (pad, parent, event);
}

gboolean
gst_triton_src_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstTritonSrcPad *srcpad = GST_TRITON_SRCPAD (pad);

  GST_TRACE_OBJECT (srcpad, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps = NULL, *filter = NULL;

      caps = gst_pad_get_pad_template_caps (pad);
      GST_DEBUG_OBJECT (srcpad, "Current caps: %" GST_PTR_FORMAT, caps);

      gst_query_parse_caps (query, &filter);
      GST_DEBUG_OBJECT (srcpad, "Filter caps: %" GST_PTR_FORMAT, filter);

      if (filter != NULL) {
        GstCaps *intersection  =
            gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (caps);
        caps = intersection;
      }

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      return TRUE;
    }
    case GST_QUERY_POSITION:
    {
      GstSegment *segment = &(srcpad)->segment;
      GstFormat format = GST_FORMAT_UNDEFINED;

      gst_query_parse_position (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_ERROR_OBJECT (srcpad, "Unsupported POSITION format: %s!",
            gst_format_get_name (format));
        return FALSE;
      }

      gst_query_set_position (query, format,
          gst_segment_to_stream_time (segment, format, segment->position));
      return TRUE;
    }
    case GST_QUERY_SEGMENT:
    {
      GstSegment *segment = &(srcpad)->segment;
      gint64 start = 0, stop = 0;

      start = gst_segment_to_stream_time (segment, segment->format,
          segment->start);

      stop = (segment->stop == GST_CLOCK_TIME_NONE) ? segment->duration :
          gst_segment_to_stream_time (segment, segment->format, segment->stop);

      gst_query_set_segment (query, segment->rate, segment->format, start, stop);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

gboolean
gst_triton_src_pad_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  gboolean success = TRUE;
  GstTritonSrcPad *srcpad = GST_TRITON_SRCPAD (pad);

  GST_INFO_OBJECT (pad, "%s worker task", active ? "Activating" : "Deactivating");

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      if (active) {
        srcpad->object = parent;
        srcpad->eos_flag = FALSE;
        srcpad->pushing_buffer = FALSE;
        // Disable requests queue in flushing state to enable normal work.
        gst_data_queue_set_flushing (GST_TRITON_SRCPAD (pad)->buffers, FALSE);
        gst_data_queue_flush (GST_TRITON_SRCPAD (pad)->buffers);
        success = gst_pad_start_task (pad, gst_triton_src_pad_worker_task,
            pad, NULL);
      } else {
        gst_data_queue_set_flushing (GST_TRITON_SRCPAD (pad)->buffers, TRUE);
        srcpad->object = NULL;
        // TODO wait for all requests.
        success = gst_pad_stop_task (pad);
      }
      break;
    default:
      break;
  }

  if (!success) {
    GST_ERROR_OBJECT (pad, "Failed to %s worker task!",
        active ? "activate" : "deactivate");
    return FALSE;
  }

  GST_INFO_OBJECT (pad, "Worker task %s", active ? "activated" : "deactivated");

  // Call the default pad handler for activate mode.
  return gst_pad_activate_mode (pad, mode, active);
}

static GstPad*
gst_triton_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstTriton *triton = GST_TRITON (element);
  GstPad *pad = NULL;
  gchar *name = NULL;
  guint index = 0, nextindex = 0;

  GST_TRITON_LOCK (triton);

  if (reqname && sscanf (reqname, "src_%u", &index) == 1) {
    // Update the next sink pad index set his name.
    nextindex = (index >= triton->nextidx) ? index + 1 : triton->nextidx;
  } else {
    index = triton->nextidx;
    // Update the index for next video pad and set his name.
    nextindex = index + 1;
  }

  GST_TRITON_UNLOCK (triton);

  name = g_strdup_printf ("src_%u", index);

  pad = g_object_new (GST_TYPE_TRITON_SRCPAD, "name", name, "direction",
      templ->direction, "template", templ, NULL);
  g_free (name);

  if (pad == NULL) {
    GST_ERROR_OBJECT (triton, "Failed to create source pad!");
    return NULL;
  }

  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_triton_src_pad_query));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_triton_src_pad_event));
  gst_pad_set_activatemode_function (pad,
      GST_DEBUG_FUNCPTR (gst_triton_src_pad_activate_mode));

  if (!gst_element_add_pad (element, pad)) {
    GST_ERROR_OBJECT (triton, "Failed to add source pad!");
    gst_object_unref (pad);
    return NULL;
  }

  GST_TRITON_LOCK (triton);

  triton->srcpads = g_list_append (triton->srcpads, pad);
  triton->nextidx = nextindex;

  GST_TRITON_UNLOCK (triton);

  GST_DEBUG_OBJECT (triton, "Created pad: %s", GST_PAD_NAME (pad));
  return pad;
}

static void
gst_triton_release_pad (GstElement * element, GstPad * pad)
{
  GstTriton *triton = GST_TRITON (element);

  GST_DEBUG_OBJECT (triton, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_TRITON_LOCK (triton);
  triton->srcpads = g_list_remove (triton->srcpads, pad);
  GST_TRITON_UNLOCK (triton);

  gst_element_remove_pad (element, pad);
}

static GstStateChangeReturn
gst_triton_change_state (GstElement * element, GstStateChange transition)
{
  GstTriton *triton = GST_TRITON(element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      triton->client = create_client(element, triton->url);
      get_model_info(element);
      gst_triton_start_worker_task (triton);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_triton_stop_worker_task (triton);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_triton_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTriton *triton = GST_TRITON(object);

  switch (prop_id) {
    case PROP_TRITON_MODE:
    {
      triton->infer_mode = g_value_get_enum (value);
      break;
    }
    case PROP_TRITON_TASK:
    {
      triton->task = g_value_get_enum (value);
      break;
    }
    case PROP_URL:
    {
      g_free (triton->url);
      triton->url = g_strdup (g_value_get_string (value));
      break;
    }
    case PROP_MODEL_NAME:
    {
      g_free (triton->model_name);
      triton->model_name = g_strdup (g_value_get_string (value));
      break;
    }
    case PROP_MODEL_VERSION:
    {
      g_free (triton->model_version);
      triton->model_version = g_strdup (g_value_get_string (value));
      break;
    }
    case PROP_LABELS:
    {
      g_free (triton->labels);
      triton->labels = g_strdup (g_value_get_string (value));
      break;
    }
    case PROP_THRESHOLD:
    {
      triton->threshold = g_value_get_double (value);
      break;
    }
    case PROP_KEEP_RATIO:
    {
      triton->keep_ratio = g_value_get_boolean (value);
      break;
    }
      default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_triton_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTriton *triton = GST_TRITON(object);

  switch (prop_id) {
    case PROP_TRITON_MODE:
    {
      g_value_set_enum (value, triton->infer_mode);
      break;
    }
    case PROP_TRITON_TASK:
    {
      g_value_set_enum (value, triton->task);
      break;
    }
    case PROP_URL:
    {
      g_value_set_string (value, triton->url);
      break;
    }
    case PROP_MODEL_NAME:
    {
      g_value_set_string (value, triton->model_name);
      break;
    }
    case PROP_MODEL_VERSION:
    {
      g_value_set_string (value, triton->model_version);
      break;
    }
    case PROP_LABELS:
    {
      g_value_set_string (value, triton->labels);
      break;
    }
    case PROP_THRESHOLD:
    {
      g_value_set_double (value, triton->threshold);
      break;
    }
    case PROP_KEEP_RATIO:
    {
      g_value_set_boolean (value, triton->keep_ratio);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_triton_finalize (GObject * object)
{
  GstTriton *triton = GST_TRITON (object);

  g_free (triton->queue_size);
  g_free (triton->url);
  g_free (triton->model_name);
  g_free (triton->model_version);
  g_free (triton->labels);
  g_rec_mutex_clear (&(triton)->worklock);
  g_mutex_clear (&(triton)->lock);
  g_cond_clear (&(triton)->wakeup);
  g_cond_clear (&(triton)->queue_is_empty);

  gst_data_queue_set_flushing (triton->requests, TRUE);
  gst_data_queue_flush (triton->requests);
  gst_object_unref (GST_OBJECT_CAST(triton->requests));

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (triton));
}

static void
gst_triton_class_init (GstTritonClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  object->set_property = GST_DEBUG_FUNCPTR (gst_triton_set_property);
  object->get_property = GST_DEBUG_FUNCPTR (gst_triton_get_property);
  object->finalize     = GST_DEBUG_FUNCPTR (gst_triton_finalize);

  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_triton_sink_template, GST_TYPE_TRITON_SINKPAD);
  gst_element_class_add_static_pad_template_with_gtype (element,
      &gst_triton_src_template, GST_TYPE_TRITON_SRCPAD);

  g_object_class_install_property (object, PROP_TRITON_MODE,
      g_param_spec_enum ("mode", "Triton Mode",
          "Arrangement of Triton inference mode",
          GST_TRITON_MODE, DEFAULT_PROP_TRITON_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object, PROP_TRITON_TASK,
      g_param_spec_enum ("task", "Triton Task",
          "Specify the type of task",
          GST_TRITON_TASK, DEFAULT_PROP_TRITON_TASK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object, PROP_URL,
      g_param_spec_string ("url", "Triton Server URL",
          "Triton server URL to create http or gRPC client",
          DEFAULT_PROP_URL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object, PROP_MODEL_NAME,
      g_param_spec_string ("model", "Model Name",
          "Model name in repository for inference",
          DEFAULT_PROP_MODEL_NAME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object, PROP_MODEL_VERSION,
      g_param_spec_string ("version", "Model Version",
          "Model version for inference model",
          DEFAULT_PROP_MODEL_VERSION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object, PROP_LABELS,
      g_param_spec_string ("labels", "Labels",
          "Labels filename",
          DEFAULT_PROP_LABELS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object, PROP_THRESHOLD,
      g_param_spec_double ("threshold", "Threshold",
          "Confidence threshold", 10.0F, 100.0F, DEFAULT_PROP_THRESHOLD,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object, PROP_KEEP_RATIO,
      g_param_spec_boolean ("ratio", "Keep Ratio",
          "Resize with fixed ratio", DEFAULT_PROP_KEEP_RATIO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "Inference by Triton server", "Video",
      "Using Triton server to do neural network inference", "QTI"
  );

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_triton_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_triton_release_pad);
  element->change_state = GST_DEBUG_FUNCPTR (gst_triton_change_state);

  // Initializes a new Triton GstDebugCategory with the given properties.
  GST_DEBUG_CATEGORY_INIT (gst_triton_debug, "qtitriton", 0, "QTI Triton");
}

static void
gst_triton_init (GstTriton * triton)
{
  GstPadTemplate *template = NULL;

  g_rec_mutex_init (&(triton)->worklock);
  g_mutex_init (&(triton)->lock);
  g_cond_init (&(triton)->wakeup);
  g_cond_init (&(triton)->queue_is_empty);

  triton->nextidx = 0;
  triton->srcpads = NULL;
  triton->client = NULL;
  triton->requests = gst_data_queue_new (queue_is_full_cb, NULL, NULL, NULL);
  triton->queue_size = g_new0 (GstDataQueueSize, 1);
  triton->infer_mode = DEFAULT_PROP_TRITON_MODE;
  triton->task = DEFAULT_PROP_TRITON_TASK;
  triton->url = DEFAULT_PROP_URL;
  triton->model_name = DEFAULT_PROP_MODEL_NAME;
  triton->model_version = DEFAULT_PROP_MODEL_VERSION;
  triton->labels = DEFAULT_PROP_LABELS;
  triton->threshold = DEFAULT_PROP_THRESHOLD;
  triton->keep_ratio = DEFAULT_PROP_KEEP_RATIO;

  template = gst_static_pad_template_get (&gst_triton_sink_template);
  triton->sinkpad = g_object_new (GST_TYPE_TRITON_SINKPAD, "name", "sink",
      "direction", template->direction, "template", template, NULL);
  gst_object_unref (template);

  gst_pad_set_chain_function (triton->sinkpad,
      GST_DEBUG_FUNCPTR (gst_triton_sink_chain));
  gst_pad_set_query_function (triton->sinkpad,
      GST_DEBUG_FUNCPTR (gst_triton_sink_pad_query));
  gst_pad_set_event_function (triton->sinkpad,
      GST_DEBUG_FUNCPTR (gst_triton_sink_pad_event));

  gst_element_add_pad (GST_ELEMENT (triton), triton->sinkpad);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtitriton", GST_RANK_NONE,
      GST_TYPE_TRITON);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtitriton,
    "QTI Triton",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
