/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "samplestitching.h"

#include <gst/video/gstqtibufferpool.h>
#include <gst/allocators/gstqtiallocator.h>
#include <gst/video/video-utils.h>
#include <gst/utils/common-utils.h>
#include <gst/video/gstimagepool.h>

#include "samplestitchingsinkpad.h"

#define GST_CAT_DEFAULT gst_sample_stitching_debug
GST_DEBUG_CATEGORY_STATIC (gst_sample_stitching_debug);

#define gst_sample_stitching_parent_class parent_class

G_DEFINE_TYPE (GstSampleStitching, gst_sample_stitching, GST_TYPE_AGGREGATOR);

#define DEFAULT_VIDEO_WIDTH         640
#define DEFAULT_VIDEO_HEIGHT        480
#define DEFAULT_VIDEO_FPS_NUM       30
#define DEFAULT_VIDEO_FPS_DEN       1

#define DEFAULT_PROP_MIN_BUFFERS    2
#define DEFAULT_PROP_MAX_BUFFERS    40

#define GST_STITCHING_MAX_QUEUE_LEN 16

#define DEFAULT_PROP_MODE           GST_SAMPLE_STITCHING_HORIZONTAL

#ifndef GST_CAPS_FEATURE_MEMORY_GBM
#define GST_CAPS_FEATURE_MEMORY_GBM "memory:GBM"
#endif

#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 1, 32767 ]"

#undef GST_VIDEO_FPS_RANGE
#define GST_VIDEO_FPS_RANGE "(fraction) [ 0, 255 ]"

#define GST_VIDEO_FORMATS \
  "{ NV12, NV21, UYVY, YUY2, RGBA, BGRA, ARGB, ABGR, RGBx, BGRx, xRGB, xBGR, RGB, BGR, GRAY8 }"

static GType gst_stitching_request_get_type(void);
#define GST_TYPE_STITCHING_REQUEST  (gst_stitching_request_get_type())
#define GST_STITCHING_REQUEST(obj) ((GstStitchingRequest *) obj)

GST_API GType gst_sample_stitching_mode_get_type (void);
#define GST_TYPE_SAMPLE_STITCHING_MODE (gst_sample_stitching_mode_get_type())

enum
{
  PROP_0,
  PROP_MODE,
};

typedef struct _GstStitchingRequest GstStitchingRequest;

struct _GstStitchingRequest {
  GstMiniObject parent;

  // List with video frames for each valid input.
  GArray        *inframes;
  // Output frame submitted with provided ID.
  GstVideoFrame *outframe;

  // Time it took for this request to be processed.
  GstClockTime  time;
};

GST_DEFINE_MINI_OBJECT_TYPE (GstStitchingRequest, gst_stitching_request);

GType
gst_sample_stitching_mode_get_type (void)
{
  static GType gtype = 0;
  static const GEnumValue variants[] = {
    { GST_SAMPLE_STITCHING_HORIZONTAL,
        "stitching by horizontal mode", "horizontal"
    },
    { GST_SAMPLE_STITCHING_VERTICAL,
        "stitching by vertical mode", "vertical"
    },
    {0, NULL, NULL},
  };

  if (!gtype)
      gtype = g_enum_register_static ("GSTSampleStitchingMode", variants);

  return gtype;
}

static GstCaps *
gst_sample_stitching_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS));

    if (gst_is_gbm_supported ()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              GST_VIDEO_FORMATS));

      caps = gst_caps_make_writable (caps);
      gst_caps_append (caps, tmplcaps);
    }

    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_sample_stitching_src_caps (void)
{
  static GstCaps *caps = NULL;
  static gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_caps_from_string (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS));

    if (gst_is_gbm_supported ()) {
      GstCaps *tmplcaps = gst_caps_from_string (
          GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GBM,
              GST_VIDEO_FORMATS));

      caps = gst_caps_make_writable (caps);
      gst_caps_append (caps, tmplcaps);
    }

    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_sample_stitching_sink_template (void)
{
  return gst_pad_template_new_with_gtype ("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST,
      gst_sample_stitching_sink_caps (), GST_TYPE_SAMPLE_STITCHING_SINKPAD);
}

static GstPadTemplate *
gst_sample_stitching_src_template (void)
{
  return gst_pad_template_new_with_gtype ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_sample_stitching_src_caps (), GST_TYPE_AGGREGATOR_PAD);
}

static void
gst_stitching_request_free (GstStitchingRequest * request)
{
  GstVideoFrame *frame = NULL;
  GstBuffer *buffer = NULL;
  guint idx = 0;

  for (idx = 0; idx < request->inframes->len; idx++) {
    frame = &(g_array_index (request->inframes, GstVideoFrame, idx));

    if ((buffer = frame->buffer) != NULL) {
      gst_video_frame_unmap (frame);
      gst_buffer_unref (buffer);
    }
  }

  if ((buffer = request->outframe->buffer) != NULL) {
    if (gst_buffer_get_size (buffer) != 0)
      gst_video_frame_unmap (request->outframe);

    gst_buffer_unref (buffer);
  }

  g_slice_free (GstVideoFrame, request->outframe);
  g_array_free (request->inframes, TRUE);
  g_slice_free (GstStitchingRequest, request);
}

static GstStitchingRequest *
gst_stitching_request_new (guint n_inputs)
{
  GstStitchingRequest *request = g_slice_new0 (GstStitchingRequest);

  gst_mini_object_init (GST_MINI_OBJECT (request), 0,
      GST_TYPE_STITCHING_REQUEST, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_stitching_request_free);

  request->inframes =
      g_array_sized_new (FALSE, TRUE, sizeof (GstVideoFrame), n_inputs);
  g_array_set_size (request->inframes, n_inputs);

  request->outframe = g_slice_new0 (GstVideoFrame);
  request->time = GST_CLOCK_TIME_NONE;

  return request;
}

static inline void
gst_stitching_request_unref (GstStitchingRequest * request)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (request));
}

static inline void
gst_video_composition_cleanup (GstSSVideoComposition * composition)
{
  guint idx = 0;

  // Free only source/destination rectangles, frames are owned by the request.
  for (idx = 0; idx < composition->n_blits; idx++) {
    g_slice_free (GstVideoRectangle, composition->blits[idx].sources);
    g_slice_free (GstVideoRectangle, composition->blits[idx].destinations);
  }

  // Free only video blits, output frame is owned by the request.
  g_free (composition->blits);
}

static inline void
gst_data_queue_item_free (gpointer data)
{
  GstDataQueueItem *item = data;
  gst_stitching_request_unref (GST_STITCHING_REQUEST (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static GstBufferPool *
gst_sample_stitching_create_pool (GstSampleStitching * stitching, GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstVideoInfo info;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (stitching, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  if (gst_is_gbm_supported ()) {
    if (gst_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_GBM)) {
      GST_INFO_OBJECT (stitching, "Uses GBM memory");
      pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_GBM);
    } else {
      GST_INFO_OBJECT (stitching, "Uses ION memory");
      pool = gst_image_buffer_pool_new (GST_IMAGE_BUFFER_POOL_TYPE_ION);
    }

    config = gst_buffer_pool_get_config (pool);
    allocator = gst_fd_allocator_new ();

    gst_buffer_pool_config_add_option (config,
        GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED);

  } else {
    GstVideoFormat format;
    GstVideoAlignment align;
    gboolean success;
    guint width, height;
    gint stride, scanline;

    width = GST_VIDEO_INFO_WIDTH (&info);
    height = GST_VIDEO_INFO_HEIGHT (&info);
    format = GST_VIDEO_INFO_FORMAT (&info);

    success = gst_adreno_utils_compute_alignment (width, height, format,
       &stride, &scanline);
    if (!success) {
      GST_ERROR_OBJECT(stitching,"Failed to get alignment");
      return NULL;
    }

    pool = gst_qti_buffer_pool_new ();
    config = gst_buffer_pool_get_config (pool);

    gst_video_alignment_reset (&align);
    align.padding_bottom = scanline - height;
    align.padding_right = stride - width;
    gst_video_info_align (&info, &align);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment (config, &align);

    allocator = gst_qti_allocator_new ();
    if (allocator == NULL) {
      GST_ERROR_OBJECT (stitching, "Failed to create QTI allocator");
      gst_clear_object (&pool);
      return NULL;
    }
  }

  gst_buffer_pool_config_set_params (config, caps, info.size,
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (stitching, "Failed to set pool configuration!");
    g_clear_object (&pool);
  }

  g_object_unref (allocator);

  return pool;
}

static gboolean
gst_sample_stitching_prepare_input_frame (GstSampleStitching * stitching,
    GstSampleStitchingSinkPad * sinkpad, GstVideoFrame * frame)
{
  GstBuffer *buffer = NULL;
  GstSegment *segment = NULL;
  GstClockTime timestamp, position;

  buffer = gst_aggregator_pad_peek_buffer (GST_AGGREGATOR_PAD (sinkpad));

  if (buffer == NULL) {
    GST_TRACE_OBJECT (sinkpad, "No buffer available!");
    return FALSE;
  }

  GST_TRACE_OBJECT (sinkpad, "Taking %" GST_PTR_FORMAT, buffer);

  segment = &GST_AGGREGATOR_PAD (GST_AGGREGATOR (stitching)->srcpad)->segment;

  // Check whether the buffer should be kept in the queue for future reuse.
  timestamp = gst_segment_to_running_time (
      &GST_AGGREGATOR_PAD (sinkpad)->segment, GST_FORMAT_TIME,
      GST_BUFFER_PTS (buffer)) + GST_BUFFER_DURATION (buffer);
  position = gst_segment_to_running_time (segment, GST_FORMAT_TIME,
      segment->position) + stitching->duration;

  if (timestamp > position)
    GST_TRACE_OBJECT (sinkpad, "Keeping buffer at least until %"
        GST_TIME_FORMAT, GST_TIME_ARGS (timestamp));
  else
    gst_aggregator_pad_drop_buffer (GST_AGGREGATOR_PAD (sinkpad));

  // GAP buffer, nothing further to do.
  if (gst_buffer_get_size (buffer) == 0 ||
      GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_GAP)) {
    gst_buffer_unref (buffer);
    return TRUE;
  }

  if (!gst_video_frame_map (frame, sinkpad->info, buffer,
      GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
    GST_ERROR_OBJECT (sinkpad, "Failed to map input buffer!");
    gst_buffer_unref (buffer);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_sample_stitching_prepare_output_frame (GstSampleStitching * stitching,
    GstVideoFrame * frame, gboolean is_gap)
{
  GstBufferPool *pool = stitching->outpool;
  GstBuffer *buffer = NULL;

  if (!is_gap) {
    if (!gst_buffer_pool_is_active (pool) &&
        !gst_buffer_pool_set_active (pool, TRUE)) {
      GST_ERROR_OBJECT (stitching, "Failed to activate output video buffer pool!");
      return FALSE;
    }

    if (gst_buffer_pool_acquire_buffer (pool, &buffer, NULL) != GST_FLOW_OK) {
      GST_ERROR_OBJECT (stitching, "Failed to create output video buffer!");
      return FALSE;
    }

    if (!gst_video_frame_map (frame, stitching->outinfo, buffer,
        GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)) {
      GST_ERROR_OBJECT (stitching, "Failed to map output buffer!");
      gst_buffer_unref (buffer);
      return FALSE;
    }
  } else {
    // Create an empty GAP buffer, which will be submitted downstream.
    buffer = gst_buffer_new ();
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_GAP);
    frame->buffer = buffer;
  }

  GST_BUFFER_DURATION (buffer) = stitching->duration;

  {
    GstSegment *s = NULL;

    GST_OBJECT_LOCK (stitching);
    s = &GST_AGGREGATOR_PAD (GST_AGGREGATOR (stitching)->srcpad)->segment;

    GST_BUFFER_TIMESTAMP (buffer) = (s->position == GST_CLOCK_TIME_NONE ||
        s->position <= s->start) ? s->start : s->position;

    s->position = GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
    GST_OBJECT_UNLOCK (stitching);
  }

  GST_TRACE_OBJECT (stitching, "Output %" GST_PTR_FORMAT, buffer);
  return TRUE;
}

static gboolean
gst_sample_stitching_populate_frames_and_composition (
    GstSampleStitching * stitching, GArray * inframes, GstVideoFrame * outframe,
    GstSSVideoComposition * composition)
{
  GList *list = NULL;
  gboolean is_gap = FALSE;
  guint idx = 0, num = 0;

  GST_OBJECT_LOCK (stitching);

  // Extrapolate the highest width, height and frame rate from the sink pads.
  for (list = GST_ELEMENT (stitching)->sinkpads; list; list = list->next, idx++) {
    GstSampleStitchingSinkPad *sinkpad = GST_SAMPLE_STITCHING_SINKPAD (list->data);
    GstVideoFrame *inframe = &(g_array_index (inframes, GstVideoFrame, idx));
    GstSSVideoBlit *blit = NULL;

    if (gst_aggregator_pad_is_eos (GST_AGGREGATOR_PAD (sinkpad)))
      continue;

    if (!gst_sample_stitching_prepare_input_frame (stitching, sinkpad, inframe)) {
      GST_TRACE_OBJECT (stitching, "Failed to prepare input frame!");
      GST_OBJECT_UNLOCK (stitching);
      return FALSE;
    }

    // GAP input buffer, nothing to do.
    if (inframe->buffer == NULL)
      continue;

    num = composition->n_blits++;

    composition->blits =
        g_renew (GstSSVideoBlit, composition->blits, composition->n_blits);

    blit = &(composition->blits[num]);
    blit->frame = inframe;

    GST_SAMPLE_STITCHING_SINKPAD_LOCK (sinkpad);

    blit->sources = g_slice_new(GstVideoRectangle);
    blit->destinations = g_slice_dup (GstVideoRectangle, &(sinkpad->destination));

    if ((blit->sources[0].w == 0) && (blit->sources[0].h == 0)) {
      blit->sources[0].w = GST_VIDEO_FRAME_WIDTH (blit->frame);
      blit->sources[0].h = GST_VIDEO_FRAME_HEIGHT (blit->frame);
    }

    if ((blit->destinations[0].w == 0) && (blit->destinations[0].h == 0)) {
      blit->destinations[0].w = GST_VIDEO_INFO_WIDTH (stitching->outinfo);
      blit->destinations[0].h = GST_VIDEO_INFO_HEIGHT (stitching->outinfo);
    }

    GST_SAMPLE_STITCHING_SINKPAD_UNLOCK (sinkpad);
  }

  GST_OBJECT_UNLOCK (stitching);

  // Whether to allocate a GAP output buffer.
  is_gap = (composition->n_blits == 0) ? TRUE : FALSE;

  if (!gst_sample_stitching_prepare_output_frame (stitching, outframe, is_gap)) {
    GST_ERROR_OBJECT (stitching, "Failed to prepae output frame!");
    return FALSE;
  }

  composition->frame = outframe;

  return TRUE;
}

static gboolean
gst_sample_stitching_propose_allocation (GstAggregator * aggregator,
    GstAggregatorPad * pad, GstQuery * inquery, GstQuery * outquery)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING_CAST (aggregator);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstVideoInfo info;
  guint size = 0;
  gboolean needpool = FALSE;

  GST_DEBUG_OBJECT (stitching, "Pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  // Extract caps from the query.
  gst_query_parse_allocation (outquery, &caps, &needpool);

  if (NULL == caps) {
    GST_ERROR_OBJECT (stitching, "Failed to extract caps from query!");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (stitching, "Failed to get video info!");
    return FALSE;
  }

  // Get the size from video info.
  size = GST_VIDEO_INFO_SIZE (&info);

  if (needpool) {
    GstStructure *structure = NULL;
    GstAllocator *allocator = NULL;

    pool = gst_sample_stitching_create_pool (stitching, caps);
    structure = gst_buffer_pool_get_config (pool);

    // Set caps and size in query.
    gst_buffer_pool_config_set_params (structure, caps, size, 0, 0);

    gst_buffer_pool_config_get_allocator (structure, &allocator, NULL);
    gst_query_add_allocation_param (outquery, allocator, NULL);

    if (!gst_buffer_pool_set_config (pool, structure)) {
      GST_ERROR_OBJECT (stitching, "Failed to set buffer pool configuration!");
      gst_object_unref (pool);
      return FALSE;
    }
  }

  // If upstream does't have a pool requirement, set only size in query.
  gst_query_add_allocation_pool (outquery, needpool ? pool : NULL, size, 0, 0);

  if (pool != NULL)
    gst_object_unref (pool);

  gst_query_add_allocation_meta (outquery, GST_VIDEO_META_API_TYPE, NULL);
  return TRUE;
}

static gboolean
gst_sample_stitching_decide_allocation (GstAggregator * aggregator,
    GstQuery * query)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING_CAST (aggregator);
  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  guint size, minbuffers, maxbuffers;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (stitching, "Failed to parse the decide_allocation caps!");
    return FALSE;
  }

  // Invalidate the cached pool if there is an allocation_query.
  if (stitching->outpool) {
    gst_buffer_pool_set_active (stitching->outpool, FALSE);
    gst_object_unref (stitching->outpool);
  }

  // Create a new buffer pool.
  pool = gst_sample_stitching_create_pool (stitching, caps);
  stitching->outpool = pool;

  {
    GstStructure *config = NULL;
    GstAllocator *allocator = NULL;
    GstAllocationParams params;

    // Get the configured pool properties in order to set in query.
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &caps, &size, &minbuffers,
        &maxbuffers);

    if (gst_buffer_pool_config_get_allocator (config, &allocator, &params))
      gst_query_add_allocation_param (query, allocator, &params);

    gst_structure_free (config);
  }

  // Check whether the query has pool.
  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, minbuffers,
        maxbuffers);
  else
    gst_query_add_allocation_pool (query, pool, size, minbuffers,
        maxbuffers);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
gst_sample_stitching_sink_query (GstAggregator * aggregator,
    GstAggregatorPad * pad, GstQuery * query)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (aggregator);

  GST_TRACE_OBJECT (stitching, "Received %s query on pad %s:%s",
      GST_QUERY_TYPE_NAME (query), GST_DEBUG_PAD_NAME (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter = NULL, *caps = NULL;

      gst_query_parse_caps (query, &filter);
      caps = gst_sample_stitching_sinkpad_getcaps (pad, aggregator, filter);
      gst_query_set_caps_result (query, caps);

      gst_caps_unref (caps);
      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_query_parse_accept_caps (query, &caps);
      success = gst_sample_stitching_sinkpad_acceptcaps (pad, aggregator, caps);
      gst_query_set_accept_caps_result (query, success);

      return TRUE;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_query (
      aggregator, pad, query);
}

static gboolean
gst_sample_stitching_sink_event (GstAggregator * aggregator,
    GstAggregatorPad * pad, GstEvent * event)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (aggregator);

  GST_TRACE_OBJECT (stitching, "Received %s event on pad %s:%s",
      GST_EVENT_TYPE_NAME (event), GST_DEBUG_PAD_NAME (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;
      gboolean success = FALSE;

      gst_event_parse_caps (event, &caps);
      success = gst_sample_stitching_sinkpad_setcaps (pad, aggregator, caps);

      gst_event_unref (event);
      return success;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_event (
      aggregator, pad, event);
}

static gboolean
gst_sample_stitching_src_query (GstAggregator * aggregator, GstQuery * query)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (aggregator);

  GST_TRACE_OBJECT (stitching, "Received %s query on src pad",
      GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstSegment *segment = &GST_AGGREGATOR_PAD (aggregator->srcpad)->segment;
      GstFormat format = GST_FORMAT_UNDEFINED;

      gst_query_parse_position (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_ERROR_OBJECT (stitching, "Unsupported POSITION format: %s!",
            gst_format_get_name (format));
        return FALSE;
      }

      gst_query_set_position (query, format,
          gst_segment_to_stream_time (segment, format, segment->position));
      return TRUE;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->src_query (aggregator, query);
}

static gboolean
gst_sample_stitching_src_event (GstAggregator * aggregator, GstEvent * event)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (aggregator);

  GST_TRACE_OBJECT (stitching, "Received %s event on src pad",
      GST_EVENT_TYPE_NAME (event));

  return GST_AGGREGATOR_CLASS (parent_class)->src_event (aggregator, event);
}

static GstFlowReturn
gst_sample_stitching_update_src_caps (GstAggregator * aggregator,
    GstCaps * caps, GstCaps ** othercaps)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (aggregator);
  gint outwidth = 0, outheight = 0, out_fps_n = 0, out_fps_d = 0;
  guint idx = 0, length = 0;
  gboolean configured = TRUE;

  GST_DEBUG_OBJECT (stitching, "Update output caps based on caps %"
      GST_PTR_FORMAT, caps);

  {
    GstSampleStitchingSinkPad *sinkpad = NULL;
    GList *list = NULL;

    GST_OBJECT_LOCK (stitching);

    // Extrapolate the highest width, height and frame rate from the sink pads.
    for (list = GST_ELEMENT (stitching)->sinkpads; list; list = list->next) {
      gint width, height, fps_n, fps_d;
      gdouble fps = 0.0, outfps = 0;

      sinkpad = GST_SAMPLE_STITCHING_SINKPAD_CAST (list->data);

      if (NULL == sinkpad->info) {
        GST_DEBUG_OBJECT (stitching, "%s caps not set!", GST_PAD_NAME (sinkpad));
        configured = FALSE;
        continue;
      }

      GST_SAMPLE_STITCHING_SINKPAD_LOCK (sinkpad);

      width = (sinkpad->destination.w != 0) ?
          sinkpad->destination.w : GST_VIDEO_INFO_WIDTH (sinkpad->info);
      height = (sinkpad->destination.h != 0) ?
          sinkpad->destination.h : GST_VIDEO_INFO_HEIGHT (sinkpad->info);

      fps_n = GST_VIDEO_INFO_FPS_N (sinkpad->info);
      fps_d = GST_VIDEO_INFO_FPS_D (sinkpad->info);

      GST_SAMPLE_STITCHING_SINKPAD_UNLOCK (sinkpad);

      if (width == 0 || height == 0)
        continue;

      // Take the greater dimensions.
      if (stitching->mode == GST_SAMPLE_STITCHING_HORIZONTAL) {
        outwidth = (2 * width > outwidth) ? 2 * width : outwidth;
        outheight = (height > outheight) ? height : outheight;
      } else if (stitching->mode == GST_SAMPLE_STITCHING_VERTICAL) {
        outwidth = (width > outwidth) ? width : outwidth;
        outheight = (2 * height > outheight) ? 2 * height : outheight;
      }

      gst_util_fraction_to_double (fps_n, fps_d, &fps);

      if (out_fps_d != 0)
        gst_util_fraction_to_double (out_fps_n, out_fps_d, &outfps);

      if (outfps < fps) {
        out_fps_n = fps_n;
        out_fps_d = fps_d;
      }
    }

    GST_OBJECT_UNLOCK (stitching);
  }

  *othercaps = gst_caps_new_empty ();
  length = gst_caps_get_size (caps);

  for (idx = 0; idx < length; idx++) {
    GstStructure *structure = gst_caps_get_structure (caps, idx);
    GstCapsFeatures *features = gst_caps_get_features (caps, idx);
    const GValue *framerate = NULL;
    gint width = 0, height = 0;

    // If this is already expressed by the existing caps skip this structure.
    if (idx > 0 && gst_caps_is_subset_structure_full (*othercaps, structure, features))
      continue;

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    gst_structure_get_int (structure, "width", &width);
    gst_structure_get_int (structure, "height", &height);
    framerate = gst_structure_get_value (structure, "framerate");

    if (!width && !outwidth) {
      gst_structure_set (structure, "width", G_TYPE_INT,
          DEFAULT_VIDEO_WIDTH, NULL);
      GST_DEBUG_OBJECT (stitching, "Width not set, using default value: %d",
          DEFAULT_VIDEO_WIDTH);
    } else if (!width) {
      gst_structure_set (structure, "width", G_TYPE_INT, outwidth, NULL);
      GST_DEBUG_OBJECT (stitching, "Width not set, using extrapolated width "
          "based on the sinkpads: %d", outwidth);
    } else if (width < outwidth) {
      GST_ERROR_OBJECT (stitching, "Set width (%u) is not compatible with the "
          "extrapolated width (%d) from the sinkpads!", width, outwidth);
      gst_structure_free (structure);
      gst_caps_unref (*othercaps);
      return GST_FLOW_NOT_SUPPORTED;
    }

    if (!height && !outheight) {
      gst_structure_set (structure, "height", G_TYPE_INT,
          DEFAULT_VIDEO_HEIGHT, NULL);
      GST_DEBUG_OBJECT (stitching, "Height not set, using default value: %d",
          DEFAULT_VIDEO_HEIGHT);
    } else if (!height) {
      gst_structure_set (structure, "height", G_TYPE_INT, outheight, NULL);
      GST_DEBUG_OBJECT (stitching, "Height not set, using extrapolated height "
          "based on the sinkpads: %d", outheight);
    } else if (height < outheight) {
      GST_ERROR_OBJECT (stitching, "Set height (%u) is not compatible with the "
          "extrapolated height (%d) from the sinkpads!", height, outheight);
      gst_structure_free (structure);
      gst_caps_unref (*othercaps);
      return GST_FLOW_NOT_SUPPORTED;
    }

    if (!gst_value_is_fixed (framerate) && (out_fps_n <= 0 || out_fps_d <= 0)) {
      gst_structure_fixate_field_nearest_fraction (structure, "framerate",
          DEFAULT_VIDEO_FPS_NUM, DEFAULT_VIDEO_FPS_DEN);
      GST_DEBUG_OBJECT (stitching, "Frame rate not set, using default value: "
          "%d/%d", DEFAULT_VIDEO_FPS_NUM, DEFAULT_VIDEO_FPS_DEN);
    } else if (!gst_value_is_fixed (framerate)) {
      gst_structure_fixate_field_nearest_fraction (structure, "framerate",
          out_fps_n, out_fps_d);
      GST_DEBUG_OBJECT (stitching, "Frame rate not set, using extrapolated "
          "rate (%d/%d) from the sinkpads", out_fps_n, out_fps_d);
    } else {
      gint fps_n = gst_value_get_fraction_numerator (framerate);
      gint fps_d = gst_value_get_fraction_denominator (framerate);
      gdouble fps = 0.0, outfps = 0.0;

      gst_util_fraction_to_double (fps_n, fps_d, &fps);
      gst_util_fraction_to_double (out_fps_n, out_fps_d, &outfps);

      if (fps != outfps) {
        GST_ERROR_OBJECT (stitching, "Set framerate (%d/%d) is not compatible"
            " with the extrapolated rate (%d/%d) from the sinkpads!", fps_n,
            fps_d, out_fps_n, out_fps_d);
        gst_structure_free (structure);
        gst_caps_unref (*othercaps);
        return GST_FLOW_NOT_SUPPORTED;
      }
    }

    gst_structure_fixate_field (structure, "format");

    framerate = gst_structure_get_value (structure, "framerate");
    stitching->duration = gst_util_uint64_scale_int (GST_SECOND,
        gst_value_get_fraction_denominator (framerate),
        gst_value_get_fraction_numerator (framerate));

    gst_caps_append_structure_full (*othercaps, structure,
        gst_caps_features_copy (features));
  }

  GST_DEBUG_OBJECT (stitching, "Updated caps %" GST_PTR_FORMAT, *othercaps);

  if (!configured)
    gst_pad_mark_reconfigure (GST_AGGREGATOR_SRC_PAD (stitching));

  return configured ? GST_FLOW_OK : GST_AGGREGATOR_FLOW_NEED_DATA;
}

static GstCaps *
gst_sample_stitching_fixate_src_caps (GstAggregator * aggregator, GstCaps * caps)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (aggregator);
  guint idx = 0;

  // Check caps structures for memory:GBM feature.
  for (idx = 0; idx < gst_caps_get_size (caps); idx++) {
    GstCapsFeatures *features = gst_caps_get_features (caps, idx);

    if (!gst_caps_features_is_any (features) &&
        gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_GBM)) {
      // Found caps structure with memory:GBM feature, remove all others.
      GstStructure *structure = gst_caps_steal_structure (caps, idx);

      gst_caps_unref (caps);
      caps = gst_caps_new_empty ();

      gst_caps_append_structure_full (caps, structure,
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GBM, NULL));
      break;
    }
  }

  caps = gst_caps_fixate (caps);
  GST_DEBUG_OBJECT (stitching, "Fixated output caps to %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
gst_sample_stitching_negotiated_src_caps (GstAggregator * aggregator,
    GstCaps * caps)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (aggregator);
  GstVideoInfo info;
  gint dar_n = 0, dar_d = 0;

  GST_DEBUG_OBJECT (stitching, "Negotiated caps %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (stitching, "Failed to get video info from caps!");
    return FALSE;
  }

  if (!gst_util_fraction_multiply (info.width, info.height,
          info.par_n, info.par_d, &dar_n, &dar_d)) {
    GST_WARNING_OBJECT (stitching, "Failed to calculate DAR!");
    dar_n = dar_d = -1;
  }

  GST_DEBUG_OBJECT (stitching, "Output %dx%d (PAR: %d/%d, DAR: %d/%d), size"
      " %" G_GSIZE_FORMAT, info.width, info.height, info.par_n, info.par_d,
      dar_n, dar_d, info.size);

  if (stitching->outinfo != NULL)
    gst_video_info_free (stitching->outinfo);

  stitching->outinfo = gst_video_info_copy (&info);

  gst_aggregator_set_latency (aggregator, stitching->duration,
      stitching->duration);

  return TRUE;
}

static void
gst_sample_stitching_task_loop (gpointer userdata)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (userdata);
  GstDataQueueItem *item = NULL;

  if (gst_data_queue_pop (stitching->requests, &item)) {
    GstStitchingRequest *request = NULL;
    GstBuffer *buffer = NULL;

    // Increase the request reference count to indicate that it is in use.
    request = GST_STITCHING_REQUEST (gst_mini_object_ref (item->object));
    item->destroy (item);

    // Get time difference between current time and start.
    request->time = GST_CLOCK_DIFF (request->time, gst_util_get_timestamp ());

    // Increase the buffer reference count to indicate that it is in use.
    buffer = gst_buffer_ref (request->outframe->buffer);
    gst_stitching_request_unref (request);

    gst_aggregator_finish_buffer (GST_AGGREGATOR (stitching), buffer);
  } else {
    GST_DEBUG_OBJECT (stitching, "Paused worker thread");
    gst_task_pause (stitching->worktask);
  }
}

static gboolean
gst_sample_stitching_start (GstAggregator * aggregator)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (aggregator);

  if (stitching->worktask != NULL)
    return TRUE;

  stitching->worktask =
      gst_task_new (gst_sample_stitching_task_loop, aggregator, NULL);
  GST_INFO_OBJECT (stitching, "Created task %p", stitching->worktask);

  gst_task_set_lock (stitching->worktask, &stitching->worklock);

  if (!gst_task_start (stitching->worktask)) {
    GST_ERROR_OBJECT (stitching, "Failed to start worker task!");
    return FALSE;
  }

  // Disable requests queue in flushing state to enable normal work.
  gst_data_queue_set_flushing (stitching->requests, FALSE);
  return TRUE;
}

static gboolean
gst_sample_stitching_stop (GstAggregator * aggregator)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (aggregator);

  if (NULL == stitching->worktask)
    return TRUE;

  // Set the requests queue in flushing state.
  gst_data_queue_set_flushing (stitching->requests, TRUE);

  if (!gst_task_stop (stitching->worktask))
    GST_WARNING_OBJECT (stitching, "Failed to stop worker task!");

  // Make sure task is not running.
  g_rec_mutex_lock (&stitching->worklock);
  g_rec_mutex_unlock (&stitching->worklock);

  if (!gst_task_join (stitching->worktask)) {
    GST_ERROR_OBJECT (stitching, "Failed to join worker task!");
    return FALSE;
  }

  // Flush converter and requests queue.
  gst_data_queue_flush (stitching->requests);

  GST_INFO_OBJECT (stitching, "Removing task %p", stitching->worktask);

  gst_object_unref (stitching->worktask);
  stitching->worktask = NULL;

  return TRUE;
}

static GstClockTime
gst_sample_stitching_get_next_time (GstAggregator * aggregator)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (aggregator);
  GstSegment *segment = &GST_AGGREGATOR_PAD (aggregator->srcpad)->segment;
  GstClockTime nexttime;

  GST_OBJECT_LOCK (stitching);
  nexttime = (segment->position == GST_CLOCK_TIME_NONE ||
      segment->position < segment->start) ? segment->start : segment->position;

  if (segment->stop != GST_CLOCK_TIME_NONE && nexttime > segment->stop)
    nexttime = segment->stop;

  nexttime = gst_segment_to_running_time (segment, GST_FORMAT_TIME, nexttime);
  GST_OBJECT_UNLOCK (stitching);

  return nexttime;
}

static gboolean
gst_sample_stitching_is_eos (GstAggregator * aggregator)
{
  GList *list = NULL;
  gboolean eos = TRUE;

  GST_OBJECT_LOCK (aggregator);

  // Iterate over every sink pad and check whether they reached EOS.
  for (list = GST_ELEMENT (aggregator)->sinkpads; list; list = list->next)
    eos &= gst_aggregator_pad_is_eos (GST_AGGREGATOR_PAD (list->data));

  GST_OBJECT_UNLOCK (aggregator);
  GST_DEBUG ("EOS is %s", eos ? "TRUE" : "FALSE");
  return eos;
}

static gboolean
gst_sample_stitching_stitch_NV12 (GstAggregator * aggregator, GstSSVideoComposition * composition)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (aggregator);
  if (composition->n_blits == 2) {
    GstVideoFrame *src1  = composition->blits[0].frame;
    GstVideoFrame *src2  = composition->blits[1].frame;
    GstVideoFrame *dest  = composition->frame;

    if (stitching->mode == GST_SAMPLE_STITCHING_HORIZONTAL) {
      for (int y = 0; y < src1->info.height; y++) {
        guint8 *src1_y = src1->data[0] + y * src1->info.stride[0];
        guint8 *src2_y = src2->data[0] + y * src2->info.stride[0];
        guint8 *dest_y = dest->data[0] + y * dest->info.stride[0];

        memcpy(dest_y, src1_y, src1->info.width);
        memcpy(dest_y + src2->info.width, src2_y, src2->info.width);
      }

      for (int y = 0; y < src1->info.height / 2; y++) {
        guint8 *src1_uv = src1->data[1] + y * src1->info.stride[1];
        guint8 *src2_uv = src2->data[1] + y * src2->info.stride[1];
        guint8 *dest_uv = dest->data[1] + y * dest->info.stride[1];

        memcpy(dest_uv, src1_uv, src1->info.width);
        memcpy(dest_uv + src2->info.width, src2_uv, src2->info.width);
      }
    } else if (stitching->mode == GST_SAMPLE_STITCHING_VERTICAL) {
      for (int y = 0; y < src1->info.height; y++) {
        guint8 *src1_y = src1->data[0] + y * src1->info.stride[0];
        guint8 *src2_y = src2->data[0] + y * src2->info.stride[0];
        guint8 *dest1_y = dest->data[0] + y * dest->info.stride[0];
        guint8 *dest2_y = dest->data[0] + (y + src1->info.height) * dest->info.stride[0];

        memcpy(dest1_y, src1_y, src1->info.width);
        memcpy(dest2_y, src2_y, src2->info.width);
      }

      for (int y = 0; y < src1->info.height / 2; y++) {
        guint8 *src1_uv = src1->data[1] + y * src1->info.stride[1];
        guint8 *src2_uv = src2->data[1] + y * src2->info.stride[1];
        guint8 *dest1_uv = dest->data[1] + y * dest->info.stride[1];
        guint8 *dest2_uv = dest->data[1] + (y + src1->info.height / 2) * dest->info.stride[1];

        memcpy(dest1_uv, src1_uv, src1->info.width);
        memcpy(dest2_uv, src2_uv, src2->info.width);
      }
    }

    GST_DEBUG ("Successfully stitch two input stream to one output stream");

    return TRUE;
  } else {
    GST_DEBUG ("Num of input streams is not equal to 2.");
    return FALSE;
  }
}

static GstFlowReturn
gst_sample_stitching_aggregate (GstAggregator * aggregator, gboolean timeout)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (aggregator);
  GstStitchingRequest *request = NULL;
  GstDataQueueItem *item = NULL;
  GstSSVideoComposition composition = GST_SS_VCE_COMPOSITION_INIT;
  gboolean success = FALSE;

  if (timeout && (NULL == stitching->outinfo))
    return GST_AGGREGATOR_FLOW_NEED_DATA;

  // Check whether all pads have reached EOS.
  if (gst_sample_stitching_is_eos (aggregator))
    return GST_FLOW_EOS;

  request = gst_stitching_request_new (stitching->n_inputs);

  success = gst_sample_stitching_populate_frames_and_composition (stitching,
      request->inframes, request->outframe, &composition);

  if (!success) {
    gst_video_composition_cleanup (&composition);
    gst_stitching_request_unref (request);
    return GST_AGGREGATOR_FLOW_NEED_DATA;
  }

  // Get start time for performance measurements.
  request->time = gst_util_get_timestamp ();

  if ((composition.blits != NULL) && (composition.n_blits != 0)) {
    success = gst_sample_stitching_stitch_NV12 (aggregator, &composition);
    gst_video_composition_cleanup (&composition);
  }

  if (!success) {
    GST_WARNING_OBJECT (stitching, "Failed to stitch streams!");
    gst_stitching_request_unref (request);
    return GST_FLOW_ERROR;
  }

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (request);
  item->visible = TRUE;
  item->destroy = gst_data_queue_item_free;

  // Push the request into the queue or free it on failure.
  if (!gst_data_queue_push (stitching->requests, item)) {
    item->destroy (item);
    return GST_FLOW_OK;
  }

  return GST_FLOW_OK;
}

static GstPad*
gst_sample_stitching_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * reqname, const GstCaps * caps)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (element);
  GstPad *pad = NULL;

  pad = GST_ELEMENT_CLASS (parent_class)->request_new_pad
      (element, templ, reqname, caps);

  if (pad == NULL) {
    GST_ERROR_OBJECT (element, "Failed to create sink pad!");
    return NULL;
  }

  GST_OBJECT_LOCK (stitching);

  // Extract the pad index field from its name.
  GST_SAMPLE_STITCHING_SINKPAD (pad)->index =
      g_ascii_strtoull (&GST_PAD_NAME (pad)[5], NULL, 10);

  stitching->n_inputs = element->numsinkpads;

  GST_OBJECT_UNLOCK (stitching);

  GST_DEBUG_OBJECT (stitching, "Created pad: %s", GST_PAD_NAME (pad));

  return pad;
}

static void
gst_sample_stitching_release_pad (GstElement * element, GstPad * pad)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (element);

  GST_DEBUG_OBJECT (stitching, "Releasing pad: %s", GST_PAD_NAME (pad));

  GST_OBJECT_LOCK (stitching);
  stitching->n_inputs = element->numsinkpads - 1;
  GST_OBJECT_UNLOCK (stitching);

  if (0 == stitching->n_inputs) {
    GstSegment *segment =
        &GST_AGGREGATOR_PAD (GST_AGGREGATOR (stitching)->srcpad)->segment;
    segment->position = GST_CLOCK_TIME_NONE;
  }

  GST_ELEMENT_CLASS (parent_class)->release_pad (GST_ELEMENT (stitching), pad);

  gst_pad_mark_reconfigure (GST_AGGREGATOR_SRC_PAD (stitching));
}

static GstStateChangeReturn
gst_sample_stitching_change_state (GstElement * element, GstStateChange transition)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
      if (stitching->n_inputs != 2) {
        GST_ERROR_OBJECT (stitching, "Num of Input Stream should be 2!");
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS) {
    GST_ERROR_OBJECT (stitching, "Failure");
    return ret;
  }

  return ret;
}

static void
gst_sample_stitching_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (object);

  GST_SAMPLE_STITCHING_LOCK (stitching);

  switch (prop_id) {
    case PROP_MODE:
      stitching->mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_SAMPLE_STITCHING_UNLOCK (stitching);
}

static void
gst_sample_stitching_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (object);

  GST_SAMPLE_STITCHING_LOCK (stitching);

  switch (prop_id) {
    case PROP_MODE:
      g_value_set_enum (value, stitching->mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_SAMPLE_STITCHING_UNLOCK (stitching);
}

static void
gst_sample_stitching_finalize (GObject * object)
{
  GstSampleStitching *stitching = GST_SAMPLE_STITCHING (object);

  if (stitching->requests != NULL) {
    gst_data_queue_set_flushing (stitching->requests, TRUE);
    gst_data_queue_flush (stitching->requests);
    gst_object_unref (GST_OBJECT_CAST(stitching->requests));
  }

  if (stitching->outpool != NULL) {
    gst_buffer_pool_set_active (stitching->outpool, FALSE);
    gst_object_unref (stitching->outpool);
  }

  if (stitching->outinfo != NULL)
    gst_video_info_free (stitching->outinfo);

  g_rec_mutex_clear (&stitching->worklock);
  g_mutex_clear (&stitching->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (stitching));
}

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
                  guint64 time, gpointer checkdata)
{
  return (visible >= GST_STITCHING_MAX_QUEUE_LEN) ? TRUE : FALSE;
}

static void
gst_sample_stitching_class_init (GstSampleStitchingClass * klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);
  GstAggregatorClass *aggregator = GST_AGGREGATOR_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_sample_stitching_debug, "qtisamplestitching", 0,
      "QTI video sample stitching");

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_sample_stitching_finalize);
  gobject->set_property = GST_DEBUG_FUNCPTR (gst_sample_stitching_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_sample_stitching_get_property);

  g_object_class_install_property (gobject, PROP_MODE,
    g_param_spec_enum ("mode", "Mode",
        "sample stitching mode",
        GST_TYPE_SAMPLE_STITCHING_MODE, DEFAULT_PROP_MODE,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "Sample Stitching", "Filter/Editor/Video/Compositor/Scaler",
      "Mix together multiple video streams", "QTI");

  gst_element_class_add_pad_template (element,
      gst_sample_stitching_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_sample_stitching_src_template ());

  element->request_new_pad = GST_DEBUG_FUNCPTR (gst_sample_stitching_request_pad);
  element->release_pad = GST_DEBUG_FUNCPTR (gst_sample_stitching_release_pad);
  element->change_state = GST_DEBUG_FUNCPTR (gst_sample_stitching_change_state);

  aggregator->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_sample_stitching_propose_allocation);
  aggregator->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_sample_stitching_decide_allocation);
  aggregator->sink_query = GST_DEBUG_FUNCPTR (gst_sample_stitching_sink_query);
  aggregator->sink_event = GST_DEBUG_FUNCPTR (gst_sample_stitching_sink_event);
  aggregator->src_event = GST_DEBUG_FUNCPTR (gst_sample_stitching_src_event);
  aggregator->src_query = GST_DEBUG_FUNCPTR (gst_sample_stitching_src_query);
  aggregator->update_src_caps =
      GST_DEBUG_FUNCPTR (gst_sample_stitching_update_src_caps);
  aggregator->fixate_src_caps =
      GST_DEBUG_FUNCPTR (gst_sample_stitching_fixate_src_caps);
  aggregator->negotiated_src_caps =
      GST_DEBUG_FUNCPTR (gst_sample_stitching_negotiated_src_caps);
  aggregator->start = GST_DEBUG_FUNCPTR (gst_sample_stitching_start);
  aggregator->stop = GST_DEBUG_FUNCPTR (gst_sample_stitching_stop);
  aggregator->get_next_time =
      GST_DEBUG_FUNCPTR (gst_sample_stitching_get_next_time);
  aggregator->aggregate = GST_DEBUG_FUNCPTR (gst_sample_stitching_aggregate);
}

static void
gst_sample_stitching_init (GstSampleStitching * stitching)
{
  g_mutex_init (&stitching->lock);
  g_rec_mutex_init (&stitching->worklock);

  stitching->n_inputs = 0;

  stitching->outinfo = NULL;
  stitching->outpool = NULL;

  stitching->duration = GST_CLOCK_TIME_NONE;

  stitching->worktask = NULL;
  stitching->requests =
      gst_data_queue_new (queue_is_full_cb, NULL, NULL, stitching);

  stitching->mode = DEFAULT_PROP_MODE;

  GST_AGGREGATOR_PAD (GST_AGGREGATOR (stitching)->srcpad)->segment.position =
      GST_CLOCK_TIME_NONE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtisamplestitching", GST_RANK_PRIMARY,
          GST_TYPE_SAMPLE_STITCHING);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtisamplestitching,
    "QTI Video Sample Stitching",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
