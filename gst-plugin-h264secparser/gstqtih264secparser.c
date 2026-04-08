/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include "gstqtih264secparser.h"

GST_DEBUG_CATEGORY_STATIC (gst_qtih264secparser_debug_category);
#define GST_CAT_DEFAULT gst_qtih264secparser_debug_category
#define parent_class gst_qtih264secparser_parent_class
/* Calculated based on sizeimage provided by driver for 1080p"*/
#define LIB_H264SEC_PARSER_PATH "libsec_h264parser.so"

#define MAX_POOL_BUFFER_SIZE 4000000
#define DEFAULT_MIN_BUFFERS 4
#define DEFAULT_MAX_BUFFERS 0

/* prototypes */

static void gst_qtih264secparser_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_qtih264secparser_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_qtih264secparser_dispose (GObject * object);

static GstCaps *gst_qtih264secparser_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_qtih264secparser_start (GstBaseTransform * trans);
static gboolean gst_qtih264secparser_stop (GstBaseTransform * trans);
static GstFlowReturn gst_qtih264secparser_submit_input_buffer (GstBaseTransform * trans,
    gboolean is_discont, GstBuffer * input);
static GstFlowReturn gst_qtih264secparser_generate_output (GstBaseTransform * trans,
    GstBuffer ** outbuf);
static gboolean gst_qtih264secparser_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);
static gboolean gst_qtih264secparser_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);

enum
{
  PROP_0,
  PROP_PASSTHROUGH,
  PROP_MIN_OUT_BUFFERS
};

/* pad templates */

static GstStaticPadTemplate gst_qtih264secparser_src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264," "stream-format = (string) { byte-stream },"
        "alignment = (string) { au }"
        ";"
        "video/x-h265," "stream-format = (string) { byte-stream },"
        "alignment = (string) { au }" ";")
    );

static GstStaticPadTemplate gst_qtih264secparser_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264," "stream-format = (string) { byte-stream },"
        "alignment = (string) { au }"
        ";"
        "video/x-h265," "stream-format = (string) { byte-stream },"
        "alignment = (string) { au }" ";")
    );

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstQtih264secparser, gst_qtih264secparser,
    GST_TYPE_BASE_TRANSFORM,
    GST_DEBUG_CATEGORY_INIT (gst_qtih264secparser_debug_category, "qtih264secparser", 0,
        "debug category for qtih264secparser element"));

static void
gst_qtih264secparser_class_init (GstQtih264secparserClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_qtih264secparser_src_template);
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_qtih264secparser_sink_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "h264secparser", "h264secparser", "Plugin for non secure to secure copy", "qualcomm.com");

  gobject_class->set_property = gst_qtih264secparser_set_property;
  gobject_class->get_property = gst_qtih264secparser_get_property;
  gobject_class->dispose = gst_qtih264secparser_dispose;

  g_object_class_install_property (gobject_class, PROP_PASSTHROUGH,
      g_param_spec_boolean ("passthrough", "Passthrough",
          "Enable passthrough mode (parse but treat entire buffer as single AU)",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MIN_OUT_BUFFERS,
      g_param_spec_uint ("min-out-buffers", "Min Output Buffers",
          "Minimum output buffer's pool size", 0, G_MAXUINT,
          DEFAULT_MIN_BUFFERS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  base_transform_class->transform_caps = GST_DEBUG_FUNCPTR (gst_qtih264secparser_transform_caps);
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_qtih264secparser_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_qtih264secparser_stop);
  base_transform_class->submit_input_buffer =
      GST_DEBUG_FUNCPTR (gst_qtih264secparser_submit_input_buffer);
  base_transform_class->generate_output = GST_DEBUG_FUNCPTR (gst_qtih264secparser_generate_output);
  base_transform_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_qtih264secparser_decide_allocation);
  base_transform_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_qtih264secparser_propose_allocation);
}

static GstBufferPool *
gst_qtih264secparser_create_pool (GstQtih264secparser * qtih264secparser)
{
  GstStructure *config = NULL;
  GstBufferPool *pool = NULL;
  GstAllocator *allocator = NULL;

  if (!(pool = gst_mem_buffer_pool_new (GST_MEMORY_BUFFER_POOL_TYPE_SECURE))) {
    GST_ERROR_OBJECT (qtih264secparser, "Failed to create new buffer pool !");
    return NULL;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, NULL, qtih264secparser->pool_buf_size,
      qtih264secparser->min_out_buffers, DEFAULT_MAX_BUFFERS);

  allocator = gst_fd_allocator_new ();
  if (NULL == allocator) {
    GST_ERROR_OBJECT (qtih264secparser, "Failed to create fd allocator !");
    g_clear_object (&pool);
    return NULL;
  }
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (qtih264secparser, "Failed to set pool configuration !");
    g_object_unref (allocator);
    g_clear_object (&pool);
    return NULL;
  }

  g_object_unref (allocator);
  return pool;
}

static void
gst_qtih264secparser_init (GstQtih264secparser *qtih264secparser)
{
  qtih264secparser->lib_handle = NULL;
  qtih264secparser->l_QSEEComHandle = NULL;
  qtih264secparser->out_pool = NULL;
  qtih264secparser->pool_buf_size = MAX_POOL_BUFFER_SIZE;
  qtih264secparser->min_out_buffers = DEFAULT_MIN_BUFFERS;
  qtih264secparser->input_buffer = NULL;
  qtih264secparser->current_au_index = 0;
  qtih264secparser->au_count = 0;
  qtih264secparser->passthrough = FALSE;
}

void
gst_qtih264secparser_set_property (GObject *object, guint property_id,
    const GValue *value, GParamSpec *pspec)
{
  GstQtih264secparser *qtih264secparser = GST_QTIH264SECPARSER (object);

  GST_DEBUG_OBJECT (qtih264secparser, "set_property");

  switch (property_id) {
    case PROP_PASSTHROUGH:
      qtih264secparser->passthrough = g_value_get_boolean (value);
      GST_INFO_OBJECT (qtih264secparser, "Passthrough mode set to %s",
          qtih264secparser->passthrough ? "enabled" : "disabled");
      break;
    case PROP_MIN_OUT_BUFFERS:
      qtih264secparser->min_out_buffers = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_qtih264secparser_get_property (GObject *object, guint property_id,
    GValue *value, GParamSpec *pspec)
{
  GstQtih264secparser *qtih264secparser = GST_QTIH264SECPARSER (object);

  GST_DEBUG_OBJECT (qtih264secparser, "get_property");

  switch (property_id) {
    case PROP_PASSTHROUGH:
      g_value_set_boolean (value, qtih264secparser->passthrough);
      break;
    case PROP_MIN_OUT_BUFFERS:
      g_value_set_uint (value, qtih264secparser->min_out_buffers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_qtih264secparser_dispose (GObject *object)
{
  GstQtih264secparser *qtih264secparser = GST_QTIH264SECPARSER (object);

  GST_DEBUG_OBJECT (qtih264secparser, "dispose");

  /* clean up as possible.  may be called multiple times */
  if (qtih264secparser->input_buffer) {
    gst_buffer_unref (qtih264secparser->input_buffer);
    qtih264secparser->input_buffer = NULL;
  }

  if (qtih264secparser->out_pool) {
    gst_buffer_pool_set_active (qtih264secparser->out_pool, FALSE);
    gst_clear_object (&qtih264secparser->out_pool);
  }

  G_OBJECT_CLASS (gst_qtih264secparser_parent_class)->dispose (object);
}

static GstCaps *
gst_qtih264secparser_transform_caps (GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, GstCaps *filter)
{
  GstQtih264secparser *qtih264secparser = GST_QTIH264SECPARSER (trans);
  GstCaps *othercaps;

  GST_DEBUG_OBJECT (qtih264secparser, "transform_caps");

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
gst_qtih264secparser_start (GstBaseTransform *trans)
{
  GstQtih264secparser *qtih264secparser = GST_QTIH264SECPARSER (trans);
  gulong res = 0;
  gchar *err = NULL;

  GST_DEBUG_OBJECT (qtih264secparser, "start");

  qtih264secparser->lib_handle = dlopen (LIB_H264SEC_PARSER_PATH, RTLD_NOW);
  if (qtih264secparser->lib_handle == NULL) {
    err = dlerror ();
    if (err != NULL)
      GST_ERROR ("Cannot load library dlerror():%s", err);

    return FALSE;
  }

  *(void **) (&qtih264secparser->hsp.H264_Secure_Parser_Set_AppName) =
      dlsym (qtih264secparser->lib_handle, "H264_Secure_Parser_Set_AppName");
  if (qtih264secparser->hsp.H264_Secure_Parser_Set_AppName == NULL) {
    GST_ERROR ("dlsym H264_Secure_Parser_Set_AppName failed!");
    if (qtih264secparser->lib_handle) {
      dlclose (qtih264secparser->lib_handle);
      qtih264secparser->lib_handle = NULL;
    }
    return FALSE;
  }

  *(void **) (&qtih264secparser->hsp.H264_Secure_Parser_Init) =
      dlsym (qtih264secparser->lib_handle, "H264_Secure_Parser_Init");
  if (qtih264secparser->hsp.H264_Secure_Parser_Init == NULL) {
    GST_ERROR ("dlsym H264_Secure_Parser_Init failed!");
    if (qtih264secparser->lib_handle) {
      dlclose (qtih264secparser->lib_handle);
      qtih264secparser->lib_handle = NULL;
    }
    return FALSE;
  }

  *(void **) (&qtih264secparser->hsp.H264_Secure_Parser) =
      dlsym (qtih264secparser->lib_handle, "H264_Secure_Parser");
  if (qtih264secparser->hsp.H264_Secure_Parser == NULL) {
    GST_ERROR ("dlsym H264_Secure_Parser failed!");
    if (qtih264secparser->lib_handle) {
      dlclose (qtih264secparser->lib_handle);
      qtih264secparser->lib_handle = NULL;
    }
    return FALSE;
  }

  *(void **) (&qtih264secparser->hsp.H264_Secure_Parser_Terminate) =
      dlsym (qtih264secparser->lib_handle, "H264_Secure_Parser_Terminate");
  if (qtih264secparser->hsp.H264_Secure_Parser_Terminate == NULL) {
    GST_ERROR ("dlsym H264_Secure_Parser_Terminate failed!");
    if (qtih264secparser->lib_handle) {
      dlclose (qtih264secparser->lib_handle);
      qtih264secparser->lib_handle = NULL;
    }
    return FALSE;
  }

  *(void **) (&qtih264secparser->hsp.Secure_Mem_Copy) =
      dlsym (qtih264secparser->lib_handle, "Secure_Mem_Copy");
  if (qtih264secparser->hsp.Secure_Mem_Copy == NULL) {
    GST_ERROR ("dlsym Secure_Mem_Copy failed!");
    if (qtih264secparser->lib_handle) {
      dlclose (qtih264secparser->lib_handle);
      qtih264secparser->lib_handle = NULL;
    }
    return FALSE;
  }

  res = qtih264secparser->hsp.H264_Secure_Parser_Set_AppName ("h264sp64");
  if (res != SEC_PARSER_SUCCESS) {
    GST_ERROR ("H264_Secure_Parser_Set_AppName failed! err:0x%08lx", res);
    if (qtih264secparser->lib_handle) {
      dlclose (qtih264secparser->lib_handle);
      qtih264secparser->lib_handle = NULL;
    }
    return FALSE;
  }

  res = qtih264secparser->hsp.H264_Secure_Parser_Init (&qtih264secparser->l_QSEEComHandle);
  if (res != SEC_PARSER_SUCCESS) {
    GST_ERROR ("H264_Secure_Parser_Init failed! err:0x%08lx", res);
    if (qtih264secparser->l_QSEEComHandle == NULL)
      GST_ERROR ("l_QSEEComHandle is NULL !!");
    if (qtih264secparser->lib_handle) {
      dlclose (qtih264secparser->lib_handle);
      qtih264secparser->lib_handle = NULL;
    }
    return FALSE;
  } else {
    GST_DEBUG_OBJECT (qtih264secparser, "hsp.H264_Secure_Parser_Init success");
  }
  return TRUE;
}

static gboolean
gst_qtih264secparser_stop (GstBaseTransform *trans)
{
  GstQtih264secparser *qtih264secparser = GST_QTIH264SECPARSER (trans);
  gulong res = 0;

  GST_DEBUG_OBJECT (qtih264secparser, "stop");

  if (qtih264secparser->l_QSEEComHandle) {
    res = qtih264secparser->hsp.H264_Secure_Parser_Terminate (&qtih264secparser->l_QSEEComHandle);
    if (res != SEC_PARSER_SUCCESS)
      GST_ERROR_OBJECT (qtih264secparser, "H264_Secure_Parser_Terminate failed! err:0x%08lx", res);
    qtih264secparser->l_QSEEComHandle = NULL;
  }

  if (qtih264secparser->lib_handle) {
    dlclose (qtih264secparser->lib_handle);
    qtih264secparser->lib_handle = NULL;
  }

  return TRUE;
}

static gboolean
gst_qtih264secparser_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstBufferPool *pool;
  guint size = 0, min = 0, max = 0;
  GstQtih264secparser *qtih264secparser = GST_QTIH264SECPARSER (trans);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    GST_DEBUG_OBJECT (qtih264secparser, "Size %d min %d max %d", size, min, max);
    qtih264secparser->pool_buf_size = size;
    qtih264secparser->min_out_buffers = min;
  }

  if (!(qtih264secparser->out_pool = gst_qtih264secparser_create_pool (qtih264secparser))) {
    GST_ERROR_OBJECT (qtih264secparser, "Failed to create output buffer pool!");
    return FALSE;
  }

  if (!gst_buffer_pool_is_active (qtih264secparser->out_pool) &&
      !gst_buffer_pool_set_active (qtih264secparser->out_pool, TRUE)) {
    GST_ERROR_OBJECT (qtih264secparser, "Failed to activate output buffer pool!");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_qtih264secparser_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstQtih264secparser *qtih264secparser = GST_QTIH264SECPARSER (trans);
  GstCaps *caps;
  GstBufferPool *pool = NULL;
  guint size = qtih264secparser->pool_buf_size;
  guint min = qtih264secparser->min_out_buffers;
  guint max = DEFAULT_MAX_BUFFERS;

  GST_DEBUG_OBJECT (qtih264secparser, "propose_allocation");

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL) {
    GST_ERROR_OBJECT (qtih264secparser, "caps is NULL in propose_allocation");
    return FALSE;
  }

  pool = gst_qtih264secparser_create_pool (qtih264secparser);
  if (pool == NULL) {
    GST_ERROR_OBJECT (qtih264secparser, "Failed to create pool in propose_allocation");
    return FALSE;
  }

  gst_query_add_allocation_pool (query, pool, size, min, max);
  gst_object_unref (pool);

  GST_DEBUG_OBJECT (qtih264secparser, "Size %d min %d max %d", size, min, max);
  if (!gst_buffer_pool_is_active (pool) && !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (qtih264secparser, "Failed to activate output buffer pool!");
    return FALSE;
  }

  qtih264secparser->out_pool = pool;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
      decide_query, query);
}

static GstFlowReturn
gst_qtih264secparser_submit_input_buffer (GstBaseTransform *trans,
    gboolean is_discont, GstBuffer *input)
{
  GstQtih264secparser *qtih264secparser = GST_QTIH264SECPARSER (trans);
  guint in_fd = -1;
  gsize buffer_size = 0;
  gsize max_buffer_size = 0;
  gulong ret = 0;
  uint32_t au_offsets[TZ_OUT_BUF_POOL_SIZE_MAX] = { 0, };
  uint32_t au_sizes[TZ_OUT_BUF_POOL_SIZE_MAX] = { 0, };
  uint32_t au_count = 0;
  guint i;

  GST_DEBUG_OBJECT (qtih264secparser, "submit_input_buffer...");

  if (gst_is_fd_memory (gst_buffer_peek_memory (input, 0))) {
    in_fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (input, 0));
  } else {
    GST_ERROR_OBJECT (qtih264secparser, "Input buffer is not fd memory!");
    gst_buffer_unref (input);
    return GST_FLOW_ERROR;
  }

  buffer_size = input->offset_end;
  max_buffer_size = gst_buffer_get_size (input);
  GST_DEBUG_OBJECT (qtih264secparser, "Input buffer FD=%d, size=%lu, max_size=%lu",
      in_fd, buffer_size, max_buffer_size);

  /* Parse the secure buffer using FD only - no mapping needed */
  ret = qtih264secparser->hsp.H264_Secure_Parser (qtih264secparser->l_QSEEComHandle,
      in_fd, buffer_size, max_buffer_size, &au_count, au_offsets, au_sizes);

  if (ret != SEC_PARSER_SUCCESS) {
    GST_ERROR_OBJECT (qtih264secparser, "H264_Secure_Parser failed! err:0x%08lx", ret);
    gst_buffer_unref (input);
    return GST_FLOW_ERROR;
  }

  /* Store AU (Access Unit) information */
  if (qtih264secparser->passthrough) {
    /* Passthrough mode: treat entire buffer as single AU */
    qtih264secparser->au_count = 1;
    qtih264secparser->au_units[0].offset = 0;
    qtih264secparser->au_units[0].size = buffer_size;
    GST_DEBUG_OBJECT (qtih264secparser,
        "Passthrough mode: treating entire buffer as single AU (size=%lu)", buffer_size);
  } else {
    /* Normal mode: use AU information from parser */
    qtih264secparser->au_count = (au_count < MAX_AU_UNITS) ? au_count : MAX_AU_UNITS;

    for (i = 0; i < qtih264secparser->au_count; i++) {
      qtih264secparser->au_units[i].offset = au_offsets[i];
      qtih264secparser->au_units[i].size = au_sizes[i];
      GST_DEBUG_OBJECT (qtih264secparser, "AU %d: offset=%u, size=%u",
          i, au_offsets[i], au_sizes[i]);
    }

    GST_DEBUG_OBJECT (qtih264secparser, "Found %u AU (Access Units)", qtih264secparser->au_count);
  }

  /* Store the input buffer for later use in generate_output */
  qtih264secparser->input_buffer = input;
  qtih264secparser->current_au_index = 0;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_qtih264secparser_generate_output (GstBaseTransform *trans, GstBuffer **outbuf)
{
  GstQtih264secparser *qtih264secparser = GST_QTIH264SECPARSER (trans);
  GstBuffer *output_buffer = NULL;
  GstMemory *mem;
  guint au_offset, au_size;

  GST_DEBUG_OBJECT (qtih264secparser, "generate_output");

  if (!qtih264secparser->input_buffer) {
    GST_ERROR_OBJECT (qtih264secparser, "No input buffer available!");
    return GST_FLOW_ERROR;
  }

  /* Check if we have more AU (Access Units) to send */
  if (qtih264secparser->current_au_index >= qtih264secparser->au_count) {
    /* All AU have been sent */
    gst_buffer_unref (qtih264secparser->input_buffer);
    qtih264secparser->input_buffer = NULL;
    *outbuf = NULL;
    return GST_BASE_TRANSFORM_FLOW_DROPPED;
  }

  /* Get AU info from parser */
  au_offset = qtih264secparser->au_units[qtih264secparser->current_au_index].offset;
  au_size = qtih264secparser->au_units[qtih264secparser->current_au_index].size;

  GST_DEBUG_OBJECT (qtih264secparser, "Sending AU %d/%d: offset=%u, size=%u",
      qtih264secparser->current_au_index + 1, qtih264secparser->au_count, au_offset, au_size);

  mem = gst_buffer_peek_memory (qtih264secparser->input_buffer, 0);
  if (gst_is_fd_memory (mem)) {
    if (qtih264secparser->current_au_index == 0) {
      /* First AU: Create output buffer as a sub-buffer of the input */
      GstMemory *sub_mem = gst_memory_share (mem, au_offset, au_size);
      if (!sub_mem) {
        GST_ERROR_OBJECT (qtih264secparser, "Failed to create sub_mem from memory share!");
        gst_buffer_unref (qtih264secparser->input_buffer);
        qtih264secparser->input_buffer = NULL;
        return GST_FLOW_ERROR;
      }
      output_buffer = gst_buffer_new ();
      gst_buffer_append_memory (output_buffer, sub_mem);

      /* Copy metadata from input buffer */
      gst_buffer_copy_into (output_buffer, qtih264secparser->input_buffer,
          GST_BUFFER_COPY_METADATA, 0, -1);

      GST_BUFFER_OFFSET (output_buffer) = au_offset;
      GST_BUFFER_OFFSET_END (output_buffer) = au_offset + au_size;
    } else {
      /* Subsequent AUs: Get buffer from out_pool and copy data */
      GstFlowReturn ret;
      guint in_fd, out_fd;
      GstMemory *out_mem;
      gulong sec_ret;

      if (!qtih264secparser->out_pool || !gst_buffer_pool_is_active (qtih264secparser->out_pool)) {
        GST_ERROR_OBJECT (qtih264secparser, "Output buffer pool is not available or not active!");
        return GST_FLOW_ERROR;
      }

      ret = gst_buffer_pool_acquire_buffer (qtih264secparser->out_pool, &output_buffer, NULL);
      if (ret != GST_FLOW_OK) {
        GST_ERROR_OBJECT (qtih264secparser, "Failed to acquire buffer from pool!");
        return ret;
      }

      in_fd = gst_fd_memory_get_fd (mem);

      out_mem = gst_buffer_peek_memory (output_buffer, 0);
      if (!gst_is_fd_memory (out_mem)) {
        GST_ERROR_OBJECT (qtih264secparser, "Output memory is not FD memory!");
        gst_buffer_unref (output_buffer);
        return GST_FLOW_ERROR;
      }
      out_fd = gst_fd_memory_get_fd (out_mem);

      GST_DEBUG_OBJECT (qtih264secparser, "Copying AU: in_fd=%d, out_fd=%d, offset=%u, size=%u",
          in_fd, out_fd, au_offset, au_size);

      sec_ret = qtih264secparser->hsp.Secure_Mem_Copy (qtih264secparser->l_QSEEComHandle,
          out_fd, 0, qtih264secparser->pool_buf_size,
          in_fd, au_offset, au_size);

      if (sec_ret != SEC_PARSER_SUCCESS) {
        GST_ERROR_OBJECT (qtih264secparser, "Secure_Mem_Copy failed! err:0x%08lx", sec_ret);
        gst_buffer_unref (output_buffer);
        return GST_FLOW_ERROR;
      }

      /* Adjust output buffer metadata and size */
      gst_buffer_copy_into (output_buffer, qtih264secparser->input_buffer,
          GST_BUFFER_COPY_METADATA, 0, -1);

      gst_memory_resize (out_mem, 0, au_size);

      GST_BUFFER_OFFSET (output_buffer) = 0;
      GST_BUFFER_OFFSET_END (output_buffer) = au_size;
    }

    GstClockTime input_pts = GST_BUFFER_PTS (qtih264secparser->input_buffer);
    GstClockTime input_duration = GST_BUFFER_DURATION (qtih264secparser->input_buffer);
    GstClockTime output_pts = GST_BUFFER_PTS (output_buffer);
    GstClockTime output_duration = GST_BUFFER_DURATION (output_buffer);
    GST_DEBUG_OBJECT (qtih264secparser,
        "AU %d/%d - Input PTS: %" GST_TIME_FORMAT ", Input Duration: %" GST_TIME_FORMAT
        ", Output PTS: %" GST_TIME_FORMAT ", Output Duration: %" GST_TIME_FORMAT,
        qtih264secparser->current_au_index + 1, qtih264secparser->au_count,
        GST_TIME_ARGS (input_pts), GST_TIME_ARGS (input_duration), GST_TIME_ARGS (output_pts),
        GST_TIME_ARGS (output_duration));
  } else {
    GST_ERROR_OBJECT (qtih264secparser, "Memory is not FD memory!");
    gst_buffer_unref (qtih264secparser->input_buffer);
    qtih264secparser->input_buffer = NULL;
    return GST_FLOW_ERROR;
  }

  qtih264secparser->current_au_index++;
  *outbuf = output_buffer;

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin *plugin)
{

  return gst_element_register (plugin, "qtih264secparser", GST_RANK_NONE,
      GST_TYPE_QTIH264SECPARSER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtih264secparser,
    "QTI secure h264 parser plugin",
    plugin_init, PACKAGE_VERSION, PACKAGE_LICENSE, PACKAGE_SUMMARY, PACKAGE_ORIGIN)
