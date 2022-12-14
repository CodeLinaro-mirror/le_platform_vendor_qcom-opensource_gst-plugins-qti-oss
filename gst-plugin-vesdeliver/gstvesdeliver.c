// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

/**
 * SECTION:element-vesdeliver
 * @title: vesdeliver
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=xxx.mp4 ! qtdemux ! h264parse !
 * vesdeliver ! qcodec2h264dec ! queue ! waylandsink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvesdeliver.h"

GST_DEBUG_CATEGORY_STATIC (vesdeliver_debug);
#define GST_CAT_DEFAULT vesdeliver_debug

#define DEFAULT_PROP_MIN_BUFFERS      6
#define DEFAULT_PROP_MAX_BUFFERS      16

static GstStaticPadTemplate sink_tmpl = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY"));

static GstStaticPadTemplate src_tmpl = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("ANY"));

#define gst_vesdeliver_parent_class parent_class
G_DEFINE_TYPE (GstVesDeliver, gst_vesdeliver,
               GST_TYPE_BASE_TRANSFORM);

static GstFlowReturn
gst_vesdeliver_prepare_output_buffer (GstBaseTransform * base,
   GstBuffer * inbuffer, GstBuffer ** outbuffer);
static GstFlowReturn
gst_vesdeliver_transform (GstBaseTransform * trans, GstBuffer * inbuffer,
    GstBuffer * outbuffer);
static gboolean
gst_vesdeliver_decide_allocation (GstBaseTransform * base, GstQuery * query);
/* state change functions */
static GstStateChangeReturn
gst_vesdeliver_change_state (GstElement * element, GstStateChange transition);

static void
gst_vesdeliver_class_init (GstVesDeliverClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetrans_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasetrans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_vesdeliver_change_state);
  gstbasetrans_class->transform = GST_DEBUG_FUNCPTR (gst_vesdeliver_transform);
  gstbasetrans_class->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_vesdeliver_prepare_output_buffer);

//  gstbasetrans_class->decide_allocation =
//      GST_DEBUG_FUNCPTR (gst_vesdeliver_decide_allocation);

  gst_element_class_add_static_pad_template (gstelement_class, &sink_tmpl);
  gst_element_class_add_static_pad_template (gstelement_class, &src_tmpl);

  gst_element_class_set_static_metadata (gstelement_class,
      "QTI Video Element Stream Deliver",
      "Deliver/Video",
      "video deliver plugin which supports secure buffer sharing", "QTI");
}

static void
gst_vesdeliver_init (GstVesDeliver * vesdeliver)
{
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (vesdeliver), FALSE);

  vesdeliver->outpool = NULL;
}

static void
gst_vesdeliver_finalize (GObject * object)
{
  GstVesDeliver *vesdeliver = GST_VESDELIVER (object);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_vesdeliver_change_state (GstElement * element, GstStateChange transition)
{
  GstVesDeliver *vesdeliver = GST_VESDELIVER (element);
  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY: {
      GST_INFO("STATE paused to ready");
    }break;
    default: {
      break;
    }
  }
  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static gboolean
gst_vesdeliver_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstVesDeliver *vesdeliver = GST_VESDELIVER (trans);
  GST_DEBUG_OBJECT(vesdeliver, "gst_vesdeliver_decide_allocation");

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;
  GstVideoInfo info;
  gboolean update = FALSE;

  gst_query_parse_allocation (query, &caps, NULL);

  GST_DEBUG_OBJECT (vesdeliver, "allocation caps: %" GST_PTR_FORMAT, caps);
  GST_DEBUG_OBJECT (vesdeliver, "allocation params: %" GST_PTR_FORMAT, query);

  if (gst_query_get_n_allocation_params (query) > 0)
    gst_query_parse_nth_allocation_param (query, 0, NULL, &params);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    update = TRUE;
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &minbuffers, &maxbuffers);
    if (pool) {
      GST_DEBUG_OBJECT (vesdeliver, "ignore buffer pool from downstream");
      gst_object_unref (pool);
      pool = NULL;
    }
  }

  if (vesdeliver->outpool) {
    gst_object_unref (vesdeliver->outpool);
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (vesdeliver, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
  size = info.size;

  // Create a new buffer pool.
  pool = gst_vesdeliver_buffer_pool_new ();
  minbuffers = DEFAULT_PROP_MIN_BUFFERS;
  maxbuffers = DEFAULT_PROP_MAX_BUFFERS;

  GST_DEBUG_OBJECT (vesdeliver, "allocation: size:%u min:%u max:%u pool:%"
       GST_PTR_FORMAT, size, minbuffers, maxbuffers, pool);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size, minbuffers, maxbuffers);

  GST_DEBUG_OBJECT (vesdeliver, "setting pool config to %" GST_PTR_FORMAT, config);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (vesdeliver, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }

  // Get the configured pool properties in order to set in query.
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, &caps, &size, &minbuffers,
      &maxbuffers);

  gst_structure_free (config);
  config = NULL;

  GST_DEBUG_OBJECT (vesdeliver, "setting pool with size: %d, min: %d, max: %d",
                    size, minbuffers, maxbuffers);

  /* update pool info in the query */
  if (update) {
    GST_DEBUG_OBJECT (vesdeliver, "update buffer pool");
    gst_query_set_nth_allocation_pool (query, 0, pool, size, minbuffers, maxbuffers);
  } else {
    GST_DEBUG_OBJECT (vesdeliver, "new buffer pool");
    gst_query_add_allocation_pool (query, pool, size, minbuffers, maxbuffers);
  }

   vesdeliver->outpool = pool;

   return TRUE;
}

static GstFlowReturn
gst_vesdeliver_prepare_output_buffer (GstBaseTransform * trans,
   GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstVesDeliver *vesdeliver = GST_VESDELIVER (trans);

  *outbuf = gst_buffer_new_allocate(NULL, gst_buffer_get_size(inbuf), NULL);

//  if (gst_buffer_pool_acquire_buffer (vesdeliver->outpool,
//                                      outbuf, NULL) != GST_FLOW_OK) {
//    GST_ERROR("Failed to acuqire buffer");
//  }

  GST_DEBUG_OBJECT(vesdeliver, "vesdeliver prepare_output_buffer size:%d",
        gst_buffer_get_size(*outbuf));

  g_return_val_if_fail(*outbuf != NULL, GST_FLOW_ERROR);

  GST_BASE_TRANSFORM_CLASS(parent_class)->copy_metadata(trans, inbuf, *outbuf);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vesdeliver_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVesDeliver *vesdeliver = GST_VESDELIVER (trans);

  g_return_val_if_fail (gst_buffer_is_writable (outbuf), GST_FLOW_ERROR);

  GstMapInfo map = {};
  gst_buffer_map (inbuf, &map, GST_MAP_READ);
  GST_DEBUG_OBJECT (vesdeliver,
        "Input buffer %p (size %" G_GSIZE_FORMAT ", timestamp %" G_GUINT64_FORMAT
        ", offset %" G_GUINT64_FORMAT "", inbuf, map.size,
        GST_BUFFER_TIMESTAMP (inbuf), GST_BUFFER_OFFSET (inbuf));

  GstMapInfo outBufferMap = {};
  gst_buffer_map(outbuf, &outBufferMap, GST_MAP_READWRITE);
  GST_DEBUG_OBJECT (vesdeliver,
        "Output buffer %p (size %" G_GSIZE_FORMAT ", timestamp %" G_GUINT64_FORMAT
        ", offset %" G_GUINT64_FORMAT "", outbuf, outBufferMap.size,
        GST_BUFFER_TIMESTAMP (outbuf), GST_BUFFER_OFFSET (outbuf));

  memcpy(outBufferMap.data, map.data, outBufferMap.size);

  gst_buffer_unmap (inbuf, &map);
  gst_buffer_unmap (outbuf, &outBufferMap);

  return GST_FLOW_OK;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
vesdeliver_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (vesdeliver_debug, "vesdeliver", 0,
      "QTI Video Element Stream Plugin");

  return gst_element_register (plugin, "vesdeliver",
      GST_RANK_SECONDARY, GST_TYPE_VESDELIVER);
}

/* gstreamer looks for this structure to register vesdeliver */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vesdeliver,
    "QTI Video Element Stream Plugin",
    vesdeliver_init,
    PACKAGE_VERSION,
    GST_LICENSE_UNKNOWN,
    PACKAGE_NAME,
    "-")
