/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst/gstinfo.h"
#include "gst/gstbufferpool.h"
#include "gstqticodec2bufferpool.h"
#include <media/msm_media_info.h>

GST_DEBUG_CATEGORY_STATIC (gst_qticodec2_debug);
#define GST_CAT_DEFAULT gst_qticodec2_debug

#define POOL_MIN_BUFFER_COUNT 6

static GstMemory *
_gst_qticodec2_alloc_dmabuf (GstBufferPool * pool)
{
  GstQticodec2BufferPool *self_pool = GST_QTICODEC2_BUFFER_POOL_CAST (pool);
  GstAllocator *alloc = self_pool->allocator;
  GstMemory *mem = NULL;
  GstVideoInfo *info = NULL;
  GstVideoFormat format;
  BufferDescriptor buffer;

  memset (&buffer, 0, sizeof (BufferDescriptor));
  info = self_pool->info;
  format = GST_VIDEO_FORMAT_INFO_FORMAT (info->finfo);
  buffer.width = info->width;
  buffer.height = info->height;
  buffer.format = format;

  /* Note: size is not used here for graphic buffer */
  GST_DEBUG_OBJECT (pool,
      "Allocating buffer size: %lu, format: %s, width: %d, height: %d",
      info->size, gst_video_format_to_string (format), info->width,
      info->height);

  /*TODO: add support for Linear buffer */
  if (format == GST_VIDEO_FORMAT_NV12 || format == GST_VIDEO_FORMAT_NV12_UBWC) {
    buffer.pool_type = BUFFER_POOL_BASIC_GRAPHIC;

    if (!c2component_alloc (self_pool->c2_comp, &buffer)) {
      GST_ERROR_OBJECT (pool, "Failed to allocate graphic buffer");
    } else {
      GST_DEBUG_OBJECT (pool, "Allocated buffer fd: %d, size: %d",
          buffer.fd, buffer.capacity);

      /* use GstFdAllocator to allocate GBM based fd memory */
      mem =
          gst_dmabuf_allocator_alloc_with_flags (alloc, buffer.fd,
          buffer.capacity, GST_FD_MEMORY_FLAG_NONE);
    }
  }

  return mem;
}

#define gst_qticodec2_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstQticodec2BufferPool, gst_qticodec2_buffer_pool,
    GST_TYPE_BUFFER_POOL);

static const char **
gst_qticodec2_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT, NULL
  };

  return options;
}

static gboolean
gst_qticodec2_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config)
{
  GstCaps *caps;
  guint32 size, min, max;
  GstQticodec2BufferPool *self_pool = GST_QTICODEC2_BUFFER_POOL_CAST (pool);
  GstVideoInfo *info = self_pool->info;

  if (NULL == config) {
    GST_ERROR_OBJECT (pool, "null config");
    return FALSE;
  }

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min, &max)) {
    GST_ERROR_OBJECT (pool, "invalid config");
    return FALSE;
  }

  if (NULL == caps) {
    GST_INFO_OBJECT (pool, "no caps in config, ignore this config");
    return FALSE;
  } else {
    if (!gst_video_info_from_caps (info, caps)) {
      GST_ERROR_OBJECT (pool, "failed to get video info");
      return FALSE;
    }

    GST_INFO_OBJECT (pool, "%dx%d, caps %" GST_PTR_FORMAT ", format = %s",
        GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info), caps,
        gst_video_format_to_string (info->finfo->format));
  }

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);
}

static void
gst_qticodec2_buffer_pool_init (GstQticodec2BufferPool * pool)
{
  GST_DEBUG_CATEGORY_INIT (gst_qticodec2_debug,
      "qticodec2pool", 0, "QTI GST codec2.0");

  GST_DEBUG_OBJECT (pool, "QTI Codec2 pool init");
}

static GstFlowReturn
gst_qticodec2_buffer_pool_alloc (GstBufferPool * pool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstBuffer *buf;
  GstMemory *mem;

  GST_DEBUG_OBJECT (pool, "alloc buffer");

  mem = _gst_qticodec2_alloc_dmabuf (pool);
  if (!mem)
    goto no_buf;

  /* create a gst buffer */
  buf = gst_buffer_new ();

  /* insert fd memmory into the gstbuffer */
  gst_buffer_prepend_memory (buf, mem);

  if (buf == NULL)
    goto no_buf;
  else
    GST_DEBUG_OBJECT (pool, "allocated gst buffer: %p, memory: %p", buf, mem);

  *buffer = buf;
  return GST_FLOW_OK;

no_buf:
  {
    GST_WARNING_OBJECT (pool, "alloc out buffer failed!");
    *buffer = NULL;
    return GST_FLOW_ERROR;
  }
}

static void
gst_qticodec2_buffer_pool_finalize (GObject * obj)
{
  GstQticodec2BufferPool *pool = GST_QTICODEC2_BUFFER_POOL_CAST (obj);

  GST_DEBUG_OBJECT (pool, "finalize buffer pool");
  if (pool) {
    if (pool->allocator) {
      gst_object_unref (pool->allocator);
      pool->allocator = NULL;
    }
    if (pool->info) {
      g_slice_free (GstVideoInfo, pool->info);
      pool->info = NULL;
    }
  }

  G_OBJECT_CLASS (gst_qticodec2_buffer_pool_parent_class)->finalize (obj);
}

static void
gst_qticodec2_buffer_pool_class_init (GstQticodec2BufferPoolClass * klass)
{
  GObjectClass *gobj_class = (GObjectClass *) klass;
  GstBufferPoolClass *bp_class = (GstBufferPoolClass *) klass;

  gobj_class->finalize = gst_qticodec2_buffer_pool_finalize;

  bp_class->get_options = gst_qticodec2_buffer_pool_get_options;
  bp_class->set_config = gst_qticodec2_buffer_pool_set_config;
  bp_class->alloc_buffer = gst_qticodec2_buffer_pool_alloc;
}

GstBufferPool *
gst_qticodec2_buffer_pool_new (gpointer comp, BUFFER_POOL_TYPE pool_type,
    guint num_buffers, GstCaps * caps)
{
  GstQticodec2BufferPool *pool;
  GstStructure *config;
  GstVideoInfo *info;
  GstAllocationParams params = { 0, 0, 0, 0, };

  pool = (GstQticodec2BufferPool *)
      g_object_new (GST_TYPE_QTICODEC2_BUFFER_POOL, NULL);

  info = g_slice_new (GstVideoInfo);
  if (!info) {
    GST_ERROR_OBJECT (pool, "failed to create a video info");
    return NULL;
  }

  GST_INFO_OBJECT (pool,
      "gst_qticodec2_buffer_pool_new, type: %d, num_buffers: %d", pool_type,
      num_buffers);

  if (!gst_video_info_from_caps (info, caps)) {
    GST_ERROR_OBJECT (pool, "failed to get video info");
    return NULL;
  }

  /* create allocator for pool */
  pool->allocator = gst_dmabuf_allocator_new ();
  if (!pool->allocator) {
    g_slice_free (GstVideoInfo, info);
    GST_ERROR_OBJECT (pool, "failed to create dmabuf allocator");
    return NULL;
  }

  pool->c2_comp = comp;
  pool->info = info;

  config = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));

  /* set pool params and options */
  gst_buffer_pool_config_set_params (config, caps, info->size,
      POOL_MIN_BUFFER_COUNT, num_buffers);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  if (!gst_qticodec2_buffer_pool_set_config (GST_BUFFER_POOL_CAST (pool),
          config)) {
    GST_ERROR_OBJECT (pool, "failed to set config to pool");
    return NULL;
  }

  GST_INFO_OBJECT (pool, "created buffer pool %p", pool);

  return GST_BUFFER_POOL (pool);
}
