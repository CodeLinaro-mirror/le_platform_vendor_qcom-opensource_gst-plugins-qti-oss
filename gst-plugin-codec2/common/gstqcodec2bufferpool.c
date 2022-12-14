/*
* Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include <gst/gst.h>
#include "gst/gstinfo.h"
#include "gstqcodec2bufferpool.h"
#include <media/msm_media_info.h>
#include "codec2wrapper.h"

GST_DEBUG_CATEGORY_STATIC (qcodec2bufferpool_debug);
#define GST_CAT_DEFAULT qcodec2bufferpool_debug

G_DEFINE_TYPE (GstQcodec2BufferPool, gst_qcodec2_buffer_pool,
    GST_TYPE_BUFFER_POOL);


#define parent_class gst_qcodec2_buffer_pool_parent_class

/* Function will be named qcodec2bufferpool_qdata_quark() */
static G_DEFINE_QUARK (Qcodec2BufferPoolQuark, qcodec2bufferpool_qdata);

static GstFlowReturn
_buffer_pool_acquire_buffer_wrap (GstBufferPool * pool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params);
static void
_buffer_pool_release_buffer_wrap (GstBufferPool * bpool, GstBuffer * buffer);

GType
gst_video_c2buf_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] = { GST_META_TAG_VIDEO_STR, NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstVideoC2BufMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static gboolean
gst_video_c2buf_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstVideoC2BufMeta *c2buf_meta = (GstVideoC2BufMeta *) meta;
  c2buf_meta->c2_buf = NULL;

  return TRUE;
}

const GstMetaInfo *
gst_video_c2buf_meta_get_info (void)
{
  static const GstMetaInfo *video_c2buf_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & video_c2buf_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_VIDEO_C2BUF_META_API_TYPE, "GstVideoC2BufMeta",
        sizeof (GstVideoC2BufMeta),
        (GstMetaInitFunction) gst_video_c2buf_meta_init, NULL, NULL);
    g_once_init_leave ((GstMetaInfo **) & video_c2buf_meta_info,
        (GstMetaInfo *) meta);
  }
  return video_c2buf_meta_info;
}

static void
print_gst_buf (gpointer key, gpointer value, gpointer data)
{
  GST_LOG ("key:0x%lx value:%p", *(gint64 *) key, value);
}

static void
gst_qcodec2_buffer_pool_init (GstQcodec2BufferPool * pool)
{
  GST_DEBUG_CATEGORY_INIT (qcodec2bufferpool_debug,
      "qcodec2pool", 0, "QTI GST codec2.0 decoder buffer pool");
}

static const gchar **
gst_qcodec2_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_C2BUF_META,
    NULL
  };

  return options;
}

static gboolean
gst_qcodec2_buffer_pool_set_config (GstBufferPool * bpool,
    GstStructure * config)
{
  GstCaps *caps;
  guint32 size, min, max;
  GstQcodec2BufferPool *pool = GST_QCODEC2_BUFFER_POOL_CAST (bpool);

  if (NULL == config) {
    GST_ERROR_OBJECT (bpool, "null config");
    return FALSE;
  }

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min, &max)) {
    GST_ERROR_OBJECT (bpool, "invalid config");
    return FALSE;
  }

  if (NULL == caps) {
    GST_WARNING_OBJECT (bpool, "no caps in config, ignore this config");
    return FALSE;
  }

  pool->add_c2bufmeta =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_C2BUF_META);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (bpool, config);
}

static void
gst_qcodec2_buffer_pool_finalize (GObject * obj)
{
  GstQcodec2BufferPool *pool = GST_QCODEC2_BUFFER_POOL_CAST (obj);
  GHashTable *buffer_table = pool->param.buffer_table;

  GST_DEBUG_OBJECT (pool, "finalize buffer pool:%p", pool);

  if (buffer_table) {
    g_hash_table_foreach (buffer_table, print_gst_buf, NULL);
    g_hash_table_destroy (buffer_table);
  }

  if (pool->allocator) {
    GST_DEBUG_OBJECT (pool, "finalize allocator:%p ref cnt:%d", pool->allocator,
        GST_OBJECT_REFCOUNT (pool->allocator));
    gst_object_unref (pool->allocator);
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
destroy_gst_buffer (gpointer data)
{
  GstBuffer *gst_buf = (GstBuffer *) data;
  if (gst_buf) {
    GST_LOG ("destory gst buffer:%p ref_cnt:%d", gst_buf,
        GST_OBJECT_REFCOUNT (gst_buf));
    gst_buffer_unref (gst_buf);
  }
}

static gboolean
mark_meta_data_pooled (GstBuffer * buffer, GstMeta ** meta, gpointer user_data)
{
  GST_META_FLAG_SET (*meta, GST_META_FLAG_POOLED);
  GST_META_FLAG_SET (*meta, GST_META_FLAG_LOCKED);

  return TRUE;
}

static GstBuffer *
_gst_qcodec2_alloc_buf (GstBufferPool * bpool)
{
  GstQcodec2BufferPool *pool = GST_QCODEC2_BUFFER_POOL_CAST (bpool);
  GstAllocator *alloc = pool->allocator;
  GstMemory *mem = NULL;
  GstVideoInfo *info = NULL;
  GstVideoFormat format;
  BufferDescriptor buffer;
  void *c2_comp = pool->param.c2_comp;
  GstBuffer *gst_buf = NULL;
  PoolMode mode = pool->param.mode;

  memset (&buffer, 0, sizeof (BufferDescriptor));
  info = &pool->param.info;
  format = GST_VIDEO_FORMAT_INFO_FORMAT (info->finfo);
  buffer.width = info->width;
  buffer.height = info->height;
  buffer.format = format;
  buffer.ubwc_flag = pool->param.is_ubwc;

  /* Note: size is not used here for graphic buffer */
  GST_DEBUG_OBJECT (bpool,
      "Allocating buffer size: %lu, format: %s, ubwc: %d, width: %d, height: %d",
      info->size, gst_video_format_to_string (format), buffer.ubwc_flag,
      info->width, info->height);

  buffer.pool_type = BUFFER_POOL_BASIC_GRAPHIC;

  if (!c2component_alloc (c2_comp, &buffer)) {
    GST_ERROR_OBJECT (bpool, "Failed to allocate graphic buffer, format: %u",
        format);
  } else {
    GST_DEBUG_OBJECT (bpool, "Allocated buffer fd: %d, size: %d format: %u",
        buffer.fd, buffer.capacity, format);

    /* use GstDmaBufAllocator to allocate GBM based fd memory */
    if (mode == DMABUF_MODE) {
      mem =
          gst_dmabuf_allocator_alloc_with_flags (alloc, buffer.fd,
          buffer.capacity, GST_FD_MEMORY_FLAG_DONT_CLOSE);
    } else if (mode == FDBUF_MODE) {
      gst_fd_allocator_alloc (alloc, buffer.fd,
          buffer.capacity, GST_FD_MEMORY_FLAG_DONT_CLOSE);
    } else {
      GST_ERROR_OBJECT (bpool, "mode %d is not support to allocate buffer",
          mode);
      gst_buf = NULL;
    }

    if (mem) {
      gst_buf = gst_buffer_new ();
      if (gst_buf) {
        if (buffer.stride[0] > 0 || buffer.stride[1] > 0) {
          info->stride[0] = buffer.stride[0];
          info->stride[1] = buffer.stride[1];
        }

        if (buffer.offset[0] > 0 || buffer.offset[1] > 0) {
          info->offset[0] = buffer.offset[0];
          info->offset[1] = buffer.offset[1];
        }

        GstVideoMeta *meta =
            gst_buffer_add_video_meta_full (gst_buf, GST_VIDEO_FRAME_FLAG_NONE,
            GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
            GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
            info->offset, info->stride);
        if (meta) {
          GST_INFO_OBJECT (bpool, "format %s, %ux%u, stride0 %u, "
              "offset0 %" G_GSIZE_FORMAT ", offset1 %" G_GSIZE_FORMAT,
              gst_video_format_to_string (meta->format), meta->width,
              meta->height, meta->stride[0], meta->offset[0], meta->offset[1]);
        }

        gst_buffer_prepend_memory (gst_buf, mem);
        GST_DEBUG_OBJECT (bpool, "allocated gst buffer: %p, memory: %p",
            gst_buf, mem);
      }
    }
  }

  return gst_buf;
}

GstFlowReturn
gst_qcodec2_buffer_pool_alloc_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstQcodec2BufferPool *pool = GST_QCODEC2_BUFFER_POOL_CAST (bpool);
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (parent_class);
  GstFlowReturn ret = GST_FLOW_OK;
  PoolMode mode = pool->param.mode;

  switch (mode) {
    case DMABUF_MODE:
    case FDBUF_MODE:
      *buffer = _gst_qcodec2_alloc_buf (bpool);
      break;
    case DMABUF_WRAP_MODE:
    case FDBUF_WRAP_MODE:
      pclass->alloc_buffer (bpool, buffer, NULL);
      GST_DEBUG_OBJECT (bpool, "call default alloc buffer function");
      break;
  }
  return ret;
}

static GstFlowReturn
gst_qcodec2_buffer_pool_acquire_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstQcodec2BufferPool *pool = GST_QCODEC2_BUFFER_POOL_CAST (bpool);
  GstBufferPoolClass *pclass = GST_BUFFER_POOL_CLASS (parent_class);

  PoolMode mode = pool->param.mode;

  switch (mode) {
    case DMABUF_MODE:
    case FDBUF_MODE:
      pclass->acquire_buffer (bpool, buffer, NULL);
      GST_DEBUG_OBJECT (bpool, "call default acquire buffer function");
      break;
    case DMABUF_WRAP_MODE:
    case FDBUF_WRAP_MODE:
      _buffer_pool_acquire_buffer_wrap (bpool, buffer, params);
      break;
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
_buffer_pool_acquire_buffer_wrap (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstMemory *mem = NULL;
  GstBuffer *gst_buf = NULL;
  GstStructure *structure;
  GstBufferPoolAcquireParamsExt *param_ext =
      (GstBufferPoolAcquireParamsExt *) params;
  GstQcodec2BufferPool *pool = GST_QCODEC2_BUFFER_POOL_CAST (bpool);

  GstVideoInfo *vinfo = &pool->param.info;
  GHashTable *buffer_table = pool->param.buffer_table;
  gboolean is_ubwc = pool->param.is_ubwc;
  PoolMode mode = pool->param.mode;

  gint64 key = ((gint64) param_ext->fd << 32) | param_ext->meta_fd;
  gint64 *buf_key = NULL;
  GValue new_index = { 0, };
  g_value_init (&new_index, G_TYPE_UINT64);
  guint32 color_fmt = 0;
  GstVideoC2BufMeta *video_c2buf_meta = NULL;

  gst_buf = (GstBuffer *) g_hash_table_lookup (buffer_table, &key);
  if (gst_buf) {
    GST_DEBUG_OBJECT (bpool,
        "found a gst buf:%p fd:%d meta_fd:%d idx:%lu ref_cnt:%d", gst_buf,
        param_ext->fd, param_ext->meta_fd, param_ext->index,
        GST_OBJECT_REFCOUNT (gst_buf));
    /*replace buffer index with current one */
    structure =
        gst_mini_object_get_qdata (GST_MINI_OBJECT (gst_buf),
        qcodec2bufferpool_qdata_quark ());
    if (structure) {
      g_value_set_uint64 (&new_index, param_ext->index);
      gst_structure_set_value (structure, "index", &new_index);
      GST_DEBUG_OBJECT (bpool, "set index:%lu into structure",
          param_ext->index);
    }
  } else {
    /* If can't find related gst buffer in hash table by fd/meta_fd,
     * new a gst buffer, and attach buf info to it. Add a flag
     * GST_FD_MEMORY_FLAG_DONT_CLOSE to avoid double free issue since
     * underlying ion buffer is allocated in C2 allocator rather than
     * dmabuf allocator of GST.
     * GST_FD_MEMORY_FLAG_KEEP_MAPPED is used to avoid remapping
     * for the same gst buffer. The mapped address may be used by waylandsink
     * for checking whether need to create a new wayland buffer.
     */
    gst_buf = gst_buffer_new ();
    if (mode == DMABUF_WRAP_MODE) {
      mem = gst_dmabuf_allocator_alloc_with_flags (pool->allocator,
          param_ext->fd, param_ext->size,
          GST_FD_MEMORY_FLAG_DONT_CLOSE | GST_FD_MEMORY_FLAG_KEEP_MAPPED);
    } else if (mode == FDBUF_WRAP_MODE) {
      mem =
          gst_fd_allocator_alloc (pool->allocator, param_ext->fd,
          param_ext->size,
          GST_FD_MEMORY_FLAG_DONT_CLOSE | GST_FD_MEMORY_FLAG_KEEP_MAPPED);
    }
    if (G_UNLIKELY (!mem)) {
      GST_ERROR_OBJECT (bpool, "failed to allocate gst memory");
      gst_buffer_unref (gst_buf);
      gst_buf = NULL;
      ret = GST_FLOW_ERROR;
      goto done;
    }
    gst_buffer_append_memory (gst_buf, mem);

    gsize offset[GST_VIDEO_MAX_PLANES] = { 0, };
    gint stride[GST_VIDEO_MAX_PLANES] = { 0, };

    switch (GST_VIDEO_INFO_FORMAT (vinfo)) {
      case GST_VIDEO_FORMAT_NV12:
        if (is_ubwc) {
          color_fmt = COLOR_FMT_NV12_UBWC;
        } else {
          color_fmt = COLOR_FMT_NV12;
        }
        break;
      case GST_VIDEO_FORMAT_NV12_10LE32:
        color_fmt = COLOR_FMT_NV12_BPP10_UBWC;
        break;
      case GST_VIDEO_FORMAT_P010_10LE:
        color_fmt = COLOR_FMT_P010;
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    stride[0] = stride[1] =
        VENUS_Y_STRIDE (color_fmt, GST_VIDEO_INFO_WIDTH (vinfo));
    offset[0] = 0;
    offset[1] = stride[0] * VENUS_Y_SCANLINES (color_fmt,
        GST_VIDEO_INFO_HEIGHT (vinfo));

    /* Add video meta data, which is needed for downstream element. */
    GST_DEBUG_OBJECT (bpool,
        "attach video meta: width:%d height:%d offset:%lu %lu stride:%d %d planes:%d size:%lu gst size:%lu",
        GST_VIDEO_INFO_WIDTH (vinfo), GST_VIDEO_INFO_HEIGHT (vinfo), offset[0],
        offset[1], stride[0], stride[1], GST_VIDEO_INFO_N_PLANES (vinfo),
        GST_VIDEO_INFO_SIZE (vinfo), gst_buffer_get_size (gst_buf));
    gst_buffer_add_video_meta_full (gst_buf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (vinfo), GST_VIDEO_INFO_WIDTH (vinfo),
        GST_VIDEO_INFO_HEIGHT (vinfo), GST_VIDEO_INFO_N_PLANES (vinfo), offset,
        stride);

    /* Attach QTI video decoder meta */
    GstCustomMeta *qvd_meta = gst_buffer_add_custom_meta (gst_buf, "GstQVDMeta");
    if (qvd_meta) {
      GstStructure *s = gst_custom_meta_get_structure (qvd_meta);
      if (s) {
        gst_structure_set (s, "gbm-meta-fd", G_TYPE_INT, param_ext->meta_fd, NULL);
        GST_DEBUG_OBJECT (bpool, "attach QVDMeta, add meta-fd:%d", param_ext->meta_fd);
      }
    }

    /* lock all metadata and mark as pooled, we want this to remain on the buffer */
    gst_buffer_foreach_meta (gst_buf, mark_meta_data_pooled, NULL);

    buf_key = g_malloc (sizeof (gint64));
    *buf_key = key;
    g_hash_table_insert (buffer_table, buf_key, gst_buf);
    GST_DEBUG_OBJECT (bpool,
        "add a gst buf:%p fd:%d meta_fd:%d idx:%lu ref_cnt:%d", gst_buf,
        param_ext->fd, param_ext->meta_fd, param_ext->index,
        GST_OBJECT_REFCOUNT (gst_buf));

    structure = gst_structure_new_empty ("BUFFER");
    g_value_set_uint64 (&new_index, param_ext->index);
    gst_structure_set_value (structure, "index", &new_index);
    gst_mini_object_set_qdata (GST_MINI_OBJECT (gst_buf),
        qcodec2bufferpool_qdata_quark (), structure,
        (GDestroyNotify) gst_structure_free);
  }

  if (pool->add_c2bufmeta) {
    video_c2buf_meta = gst_buffer_get_video_c2buf_meta (gst_buf);
    if (video_c2buf_meta) {
      gst_buffer_remove_meta (gst_buf, (GstMeta *) video_c2buf_meta);
    }
    video_c2buf_meta = gst_buffer_add_video_c2buf_meta (gst_buf);
    if (video_c2buf_meta) {
      video_c2buf_meta->c2_buf = param_ext->c2_buf;
    }
    GST_DEBUG_OBJECT (bpool, "attach c2buf meta, c2_buf:%p", param_ext->c2_buf);
  }

done:
  *buffer = gst_buf;

  return ret;
}

static void
_buffer_pool_release_buffer_wrap (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstBufferPoolClass *bp_class = GST_BUFFER_POOL_CLASS (parent_class);
  GstQcodec2BufferPool *pool = GST_QCODEC2_BUFFER_POOL_CAST (bpool);
  void *c2_comp = pool->param.c2_comp;

  guint64 index = 0;
  GstStructure *structure = (GstStructure *) gst_mini_object_get_qdata
      (GST_MINI_OBJECT (buffer), qcodec2bufferpool_qdata_quark ());

  if (structure) {
    /* If buffer comes from sink, free it.
     * In fact, underlying C2Allocator don't free it rather than return it
     * to internal buffer pool for recycling.
     */
    gst_structure_get_uint64 (structure, "index", &index);

    if (c2_comp) {
      GST_DEBUG_OBJECT (bpool, "release output buffer index: %ld", index);
      if (!c2component_freeOutBuffer (c2_comp, index)) {
        GST_ERROR_OBJECT (bpool, "Failed to release buffer: %lu", index);
      }
    } else {
      GST_ERROR_OBJECT (bpool, "invalid c2 component");
    }
  } else {
    /* If buffer don't have this quark, means it's allocated in pre-allocation stage
     * of buffer pool. But it's not used since only gst buffer allocated in
     * gst_qcodec2vdec_buffer_pool_acquire_buffer is used. Here is used for releasing preallocated
     * gst buffer to internal queue of buffer pool. As a result, it can be freed once pool destroyed.
     */
    bp_class->release_buffer (bpool, buffer);
    GST_DEBUG_OBJECT (bpool, "release pre-allocated buffer:%p", buffer);
  }
}

static void
gst_qcodec2_buffer_pool_release_buffer (GstBufferPool * bpool,
    GstBuffer * buffer)
{
  GstBufferPoolClass *bp_class = GST_BUFFER_POOL_CLASS (parent_class);
  GstQcodec2BufferPool *pool = GST_QCODEC2_BUFFER_POOL_CAST (bpool);

  PoolMode mode = pool->param.mode;

  switch (mode) {
    case DMABUF_MODE:
    case FDBUF_MODE:
      bp_class->release_buffer (bpool, buffer);
      GST_DEBUG_OBJECT (pool, "call default release buffer function");
      break;
    case DMABUF_WRAP_MODE:
    case FDBUF_WRAP_MODE:
      _buffer_pool_release_buffer_wrap (bpool, buffer);
      break;
  }
}

static void
gst_qcodec2_buffer_pool_class_init (GstQcodec2BufferPoolClass * klass)
{
  GObjectClass *gobj_class = (GObjectClass *) klass;
  GstBufferPoolClass *bp_class = (GstBufferPoolClass *) klass;

  gobj_class->finalize = gst_qcodec2_buffer_pool_finalize;

  bp_class->get_options = gst_qcodec2_buffer_pool_get_options;
  bp_class->set_config = gst_qcodec2_buffer_pool_set_config;
  bp_class->alloc_buffer = gst_qcodec2_buffer_pool_alloc_buffer;
  bp_class->acquire_buffer = gst_qcodec2_buffer_pool_acquire_buffer;
  bp_class->release_buffer = gst_qcodec2_buffer_pool_release_buffer;
}

GstBufferPool *
gst_qcodec2_buffer_pool_new (GstBufferPoolInitParam * param)
{
  GstQcodec2BufferPool *pool = NULL;
  GHashTable *buffer_table = NULL;

  if (!param) {
    GST_ERROR ("invalid input parameter");
    return NULL;
  }

  pool = (GstQcodec2BufferPool *)
      g_object_new (GST_TYPE_QCODEC2_BUFFER_POOL, NULL);
  if (!pool) {
    GST_ERROR ("failed to create buffer pool");
    return NULL;
  }

  pool->param = *param;

  GST_DEBUG_OBJECT (pool, "pool mode:%d ubwc:%d", param->mode, param->is_ubwc);

  switch (param->mode) {
    case DMABUF_MODE:
      pool->allocator = gst_dmabuf_allocator_new ();
      break;
    case FDBUF_MODE:
      pool->allocator = gst_fd_allocator_new ();
      break;
    case DMABUF_WRAP_MODE:
      pool->allocator = gst_dmabuf_allocator_new ();
      buffer_table =
          g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free,
          destroy_gst_buffer);
      pool->param.buffer_table = buffer_table;
      break;
    case FDBUF_WRAP_MODE:
      pool->allocator = gst_fd_allocator_new ();
      buffer_table =
          g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free,
          destroy_gst_buffer);
      pool->param.buffer_table = buffer_table;
      break;
    default:
      GST_ERROR_OBJECT (pool, "pool mode %d is not supported", param->mode);
  }

  GST_INFO_OBJECT (pool,
      "new output buffer pool:%p allocator:%p table:%p ubwc:%d", pool,
      pool->allocator, buffer_table, param->is_ubwc);

  return GST_BUFFER_POOL (pool);
}
