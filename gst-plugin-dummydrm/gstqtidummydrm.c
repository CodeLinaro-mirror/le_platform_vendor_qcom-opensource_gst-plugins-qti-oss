/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include "gstqtidummydrm.h"

GST_DEBUG_CATEGORY_STATIC (gst_qtidummydrm_debug_category);
#define GST_CAT_DEFAULT gst_qtidummydrm_debug_category
#define parent_class gst_qtidummydrm_parent_class
/* Calculated based on sizeimage provided by driver for 1080p"*/

#define MAX_POOL_BUFFER_SIZE 4000000

#ifndef LIB_CONTENT_COPY_PATH
#define LIB_CONTENT_COPY_PATH "/usr/lib/libcontentcopy.so"
#endif

/* prototypes */

static void gst_qtidummydrm_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_qtidummydrm_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_qtidummydrm_dispose (GObject * object);

static GstCaps *gst_qtidummydrm_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_qtidummydrm_start (GstBaseTransform * trans);
static gboolean gst_qtidummydrm_stop (GstBaseTransform * trans);
static GstFlowReturn gst_qtidummydrm_prepare_output_buffer (GstBaseTransform *
    trans, GstBuffer * input, GstBuffer ** outbuf);
static gboolean gst_qtidummydrm_transform_decide_allocation (GstBaseTransform *
    trans, GstQuery * query);
static GstFlowReturn gst_qtidummydrm_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

enum
{
  PROP_0
};

/* pad templates */

static GstStaticPadTemplate gst_qtidummydrm_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264," "stream-format = (string) { byte-stream },"
        "alignment = (string) { au }"
        ";"
        "video/x-h265," "stream-format = (string) { byte-stream },"
        "alignment = (string) { au }" ";")
    );

static GstStaticPadTemplate gst_qtidummydrm_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264," "stream-format = (string) { byte-stream },"
        "alignment = (string) { au }"
        ";"
        "video/x-h265," "stream-format = (string) { byte-stream },"
        "alignment = (string) { au }" ";")
    );

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstQtidummydrm, gst_qtidummydrm,
    GST_TYPE_BASE_TRANSFORM,
    GST_DEBUG_CATEGORY_INIT (gst_qtidummydrm_debug_category, "qtidummydrm", 0,
        "debug category for qtidummydrm element"));

static void
gst_qtidummydrm_class_init (GstQtidummydrmClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_qtidummydrm_src_template);
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_qtidummydrm_sink_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "dummydrm", "dummydrm", "Plugin for non secure to secure copy",
      "qualcomm.com");

  gobject_class->set_property = gst_qtidummydrm_set_property;
  gobject_class->get_property = gst_qtidummydrm_get_property;
  gobject_class->dispose = gst_qtidummydrm_dispose;
  base_transform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_qtidummydrm_transform_caps);
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_qtidummydrm_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_qtidummydrm_stop);
  base_transform_class->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_qtidummydrm_prepare_output_buffer);
  base_transform_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_qtidummydrm_transform_decide_allocation);
  base_transform_class->transform =
      GST_DEBUG_FUNCPTR (gst_qtidummydrm_transform);
}

static GstBufferPool *
gst_qtihdcpdecrypt_create_pool (GstQtidummydrm * qtidummydrm)
{
  GstStructure *config = NULL;
  GstBufferPool *pool = NULL;
  GstAllocator *allocator = NULL;

  if (!(pool = gst_mem_buffer_pool_new (GST_MEMORY_BUFFER_POOL_TYPE_SECURE))) {
    GST_ERROR_OBJECT (qtidummydrm, "Failed to create new buffer pool !");
    return NULL;
  }

  config = gst_buffer_pool_get_config (pool);
  /*TODO use min max value from decide_allocation */
  gst_buffer_pool_config_set_params (config, NULL, qtidummydrm->pool_buf_size,
      DEFAULT_MIN_BUFFERS, DEFAULT_MAX_BUFFERS);

  if (!(allocator = gst_fd_allocator_new ())) {
    GST_ERROR_OBJECT (qtidummydrm, "Failed to create fd allocator !");
    g_clear_object (&pool);
    return NULL;
  }
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (qtidummydrm, "Failed to set pool configuration !");
    g_object_unref (allocator);
    g_clear_object (&pool);
      return NULL;
  }

  g_object_unref (allocator);
  return pool;
}

static void
gst_qtidummydrm_init (GstQtidummydrm * qtidummydrm)
{
  qtidummydrm->out_pool = NULL;
  qtidummydrm->lib_handle = NULL;
  qtidummydrm->l_QSEEComHandle = NULL;
  qtidummydrm->pool_buf_size = MAX_POOL_BUFFER_SIZE;
}

void
gst_qtidummydrm_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQtidummydrm *qtidummydrm = GST_QTIDUMMYDRM (object);

  GST_DEBUG_OBJECT (qtidummydrm, "set_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_qtidummydrm_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstQtidummydrm *qtidummydrm = GST_QTIDUMMYDRM (object);

  GST_DEBUG_OBJECT (qtidummydrm, "get_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_qtidummydrm_dispose (GObject * object)
{
  GstQtidummydrm *qtidummydrm = GST_QTIDUMMYDRM (object);

  GST_DEBUG_OBJECT (qtidummydrm, "dispose");

  /* clean up as possible.  may be called multiple times */
  if (qtidummydrm->out_pool) {
    gst_buffer_pool_set_active (qtidummydrm->out_pool, FALSE);
    gst_clear_object (&qtidummydrm->out_pool);
  }

  G_OBJECT_CLASS (gst_qtidummydrm_parent_class)->dispose (object);
}

static GstCaps *
gst_qtidummydrm_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstQtidummydrm *qtidummydrm = GST_QTIDUMMYDRM (trans);
  GstCaps *othercaps;

  GST_DEBUG_OBJECT (qtidummydrm, "transform_caps");

  othercaps = gst_caps_copy (caps);

  if (filter) {
    GstCaps *intersect;

    intersect = gst_caps_intersect (othercaps, filter);
    gst_caps_unref (othercaps);

    return intersect;
  } else {
    return othercaps;
  }
}

/* states */
static gboolean
gst_qtidummydrm_start (GstBaseTransform * trans)
{
  GstQtidummydrm *qtidummydrm = GST_QTIDUMMYDRM (trans);
  gulong res = 0;
  gchar *err = NULL;

  GST_DEBUG_OBJECT (qtidummydrm, "start");

  qtidummydrm->lib_handle = dlopen (LIB_CONTENT_COPY_PATH, RTLD_NOW);
  if (qtidummydrm->lib_handle == NULL) {
    err = dlerror ();
    if (err != NULL)
      GST_ERROR ("Cannot load library dlerror():%s", err);

    return FALSE;
  }

  *(void **) (&qtidummydrm->cpc.Content_Protection_Set_AppName) =
      dlsym (qtidummydrm->lib_handle, "Content_Protection_Set_AppName");
  if (qtidummydrm->cpc.Content_Protection_Set_AppName == NULL) {
    GST_ERROR ("dlsym Content_Protection_Set_AppName failed!");
    if (qtidummydrm->lib_handle) {
      dlclose (qtidummydrm->lib_handle);
      qtidummydrm->lib_handle = NULL;
    }
    return FALSE;
  }

  *(void **) (&qtidummydrm->cpc.Content_Protection_Copy_Init) =
      dlsym (qtidummydrm->lib_handle, "Content_Protection_Copy_Init");
  if (qtidummydrm->cpc.Content_Protection_Copy_Init == NULL) {
    GST_ERROR ("dlsym Content_Protection_Copy_Init failed!");
    if (qtidummydrm->lib_handle) {
      dlclose (qtidummydrm->lib_handle);
      qtidummydrm->lib_handle = NULL;
    }
    return FALSE;
  }

  *(void **) (&qtidummydrm->cpc.Content_Protection_Copy) =
      dlsym (qtidummydrm->lib_handle, "Content_Protection_Copy");
  if (qtidummydrm->cpc.Content_Protection_Copy == NULL) {
    GST_ERROR ("dlsym Content_Protection_Copy failed!");
    if (qtidummydrm->lib_handle) {
      dlclose (qtidummydrm->lib_handle);
      qtidummydrm->lib_handle = NULL;
    }
    return FALSE;
  }

  *(void **) (&qtidummydrm->cpc.Content_Protection_Copy_Terminate) =
      dlsym (qtidummydrm->lib_handle, "Content_Protection_Copy_Terminate");
  if (qtidummydrm->cpc.Content_Protection_Copy_Terminate == NULL) {
    GST_ERROR ("dlsym Content_Protection_Copy_Terminate failed!");
    if (qtidummydrm->lib_handle) {
      dlclose (qtidummydrm->lib_handle);
      qtidummydrm->lib_handle = NULL;
    }
    return FALSE;
  }

  res = qtidummydrm->cpc.Content_Protection_Set_AppName ("cntcpy64");
  if (res != SAMPLE_CLIENT_SUCCESS) {
    GST_ERROR ("Content_Protection_Set_AppName failed! err:0x%08lx", res);
    if (qtidummydrm->lib_handle) {
      dlclose (qtidummydrm->lib_handle);
      qtidummydrm->lib_handle = NULL;
    }
    return FALSE;
  }

  res =
      qtidummydrm->cpc.
      Content_Protection_Copy_Init (&qtidummydrm->l_QSEEComHandle);
  if (res != SAMPLE_CLIENT_SUCCESS) {
    GST_ERROR ("Content_Protection_Copy_Init failed! err:0x%08lx", res);
    if (qtidummydrm->l_QSEEComHandle == NULL)
      GST_ERROR ("l_QSEEComHandle is NULL !!");
    if (qtidummydrm->lib_handle) {
      dlclose (qtidummydrm->lib_handle);
      qtidummydrm->lib_handle = NULL; 
    }
    return FALSE;
  } else {
    GST_DEBUG_OBJECT (qtidummydrm, "cpc.Content_Protection_Copy_Init success");
  }
  return TRUE;
}

static gboolean
gst_qtidummydrm_stop (GstBaseTransform * trans)
{
  GstQtidummydrm *qtidummydrm = GST_QTIDUMMYDRM (trans);
  gulong res = 0;

  GST_DEBUG_OBJECT (qtidummydrm, "stop");

  if (qtidummydrm->l_QSEEComHandle) {
    res = qtidummydrm->cpc.Content_Protection_Copy_Terminate (&qtidummydrm->l_QSEEComHandle);
    if (res != SAMPLE_CLIENT_SUCCESS)
      GST_ERROR_OBJECT (qtidummydrm,
          "Content_Protection_Copy_Terminate failed! err:0x%08lx", res);
    qtidummydrm->l_QSEEComHandle = NULL;
  }

  if (qtidummydrm->lib_handle) {
    dlclose (qtidummydrm->lib_handle);
    qtidummydrm->lib_handle = NULL;
  }

  return TRUE;
}

static GstFlowReturn
gst_qtidummydrm_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstQtidummydrm *qtidummydrm = GST_QTIDUMMYDRM (trans);
  GstFlowReturn res = GST_FLOW_ERROR;

  GstBufferPool *pool = qtidummydrm->out_pool;

  GST_DEBUG_OBJECT (qtidummydrm, "prepare_output_buffer");

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (qtidummydrm, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }
  // Input is marked as GAP, nothing to process. Create a GAP output buffer.
  if (gst_buffer_get_size (inbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP)) {
    *outbuffer = gst_buffer_new ();
    gst_buffer_copy_into (*outbuffer, inbuffer, GST_BUFFER_COPY_METADATA, 0,
        -1);
    return GST_FLOW_OK;
  }

  res = gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL);
  if (res != GST_FLOW_OK) {
    GST_ERROR_OBJECT (qtidummydrm, "Failed to create output buffer!");
    return res;
  }
  (*outbuffer)->offset_end = gst_buffer_get_size (inbuffer);
  gst_buffer_copy_into (*outbuffer, inbuffer, GST_BUFFER_COPY_METADATA, 0, -1);

  return res;
}

static gboolean
gst_qtidummydrm_transform_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  guint size, min, max;
  gboolean ret = FALSE;
  GstBufferPool *pool;
  GstQtidummydrm *qtidummydrm = GST_QTIDUMMYDRM (trans);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    GST_DEBUG_OBJECT (qtidummydrm, "Size %d min %d max %d", size, min, max);
    qtidummydrm->pool_buf_size = size;
  }

  if (!(qtidummydrm->out_pool = gst_qtihdcpdecrypt_create_pool (qtidummydrm))) {
    GST_ERROR_OBJECT (qtidummydrm, "Failed to create output buffer pool!");
    return FALSE;
  }

  if (!gst_buffer_pool_is_active (qtidummydrm->out_pool) &&
      !gst_buffer_pool_set_active (qtidummydrm->out_pool, TRUE)) {
    GST_ERROR_OBJECT (qtidummydrm, "Failed to activate output buffer pool!");
    return FALSE;
  }
  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);

  return ret;
}

/* transform */
static GstFlowReturn
gst_qtidummydrm_transform (GstBaseTransform * trans, GstBuffer * inbuffer,
    GstBuffer * outbuffer)
{
  GstQtidummydrm *qtidummydrm = GST_QTIDUMMYDRM (trans);
  GstMapInfo inbuff_map_info;
  gint out_fd;
  GstFlowReturn res = GST_FLOW_OK;
  gulong ret = 0;
  uint32_t decrypt_size = 0;
  gboolean buffer_mapped = FALSE;

  GST_DEBUG_OBJECT (qtidummydrm, "gst_qtidummydrm_transform");

  if (gst_is_fd_memory (gst_buffer_peek_memory (outbuffer, 0))) {
    out_fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (outbuffer, 0));
  } else {
    GST_ERROR_OBJECT (qtidummydrm, "Output buffer is not fd memory!");
    return GST_FLOW_ERROR;
  }

  if (!gst_buffer_map (inbuffer, &inbuff_map_info, GST_MAP_READ)) {
    GST_ERROR_OBJECT (qtidummydrm, "Failed to map input buffer !");
    return GST_FLOW_ERROR;
  }

  buffer_mapped = TRUE;

  GST_DEBUG_OBJECT (qtidummydrm, "out_fd %d ", out_fd);
  decrypt_size = inbuff_map_info.size;
  ret = qtidummydrm->cpc.Content_Protection_Copy (qtidummydrm->l_QSEEComHandle,
      inbuff_map_info.data,
      inbuff_map_info.size,
      (uint32_t) out_fd,
      0,
      (uint32_t *) & decrypt_size,
      (SampleClientCopyDir) SAMPLECLIENT_COPY_NONSECURE_TO_SECURE);

  if (ret != SAMPLE_CLIENT_SUCCESS) {
    GST_ERROR ("Content_Protection_Copy_Init failed! err:%d", res);
    if (qtidummydrm->l_QSEEComHandle == NULL)
      GST_ERROR ("l_QSEEComHandle is NULL !!");
    if (qtidummydrm->lib_handle) {
      dlclose (qtidummydrm->lib_handle);
      qtidummydrm->lib_handle = NULL;
    }
    return FALSE;
  } else {
    outbuffer->offset_end = inbuff_map_info.size;
    GST_DEBUG_OBJECT (qtidummydrm, "Non Secure to Secure buffer copy SUCCESS size = %lu",
        outbuffer->offset_end);
  }

  if (buffer_mapped)
    gst_buffer_unmap (inbuffer, &inbuff_map_info);

  return res;
}

static gboolean
plugin_init (GstPlugin * plugin)
{

  return gst_element_register (plugin, "qtidummydrm", GST_RANK_NONE,
      GST_TYPE_QTIDUMMYDRM);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtidummydrm,
    "QTI non secure to secure copy plugin",
    plugin_init,
    PACKAGE_VERSION, PACKAGE_LICENSE, PACKAGE_SUMMARY, PACKAGE_ORIGIN)
