/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
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
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
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

#include "gstimagepool.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

GST_DEBUG_CATEGORY_STATIC (gst_image_pool_debug);
#define GST_CAT_DEFAULT gst_image_pool_debug

#define GST_IS_SYSTEM_MEMORY_TYPE(type) \
    (type == g_quark_from_static_string (GST_IMAGE_BUFFER_POOL_TYPE_SYS))


struct _GstImageBufferPoolPrivate
{
  GstVideoInfo        info;
  gboolean            addmeta;
  gboolean            keepmapped;

  GstAllocator        *allocator;
  GstAllocationParams params;
  GQuark              memtype;
};

#define gst_image_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstImageBufferPool, gst_image_buffer_pool,
    GST_TYPE_BUFFER_POOL);

static const gchar **
gst_image_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = {
    GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED,
    NULL
  };
  return options;
}

static gboolean
gst_image_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstImageBufferPool *vpool = GST_IMAGE_BUFFER_POOL (pool);
  GstImageBufferPoolPrivate *priv = vpool->priv;

  gboolean success;
  GstVideoInfo info;
  GstCaps *caps;
  guint size, minbuffers, maxbuffers;
  GstAllocator *allocator;
  GstAllocationParams params;

  success = gst_buffer_pool_config_get_params (config, &caps, &size,
      &minbuffers, &maxbuffers);

  if (!success) {
    GST_ERROR_OBJECT (vpool, "Invalid configuration!");
    return FALSE;
  } else if (caps == NULL) {
    GST_ERROR_OBJECT (vpool, "Caps missing from configuration");
    return FALSE;
  }

  // Now parse the caps from the configuration.
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (vpool, "Failed getting geometry from caps %"
        GST_PTR_FORMAT, caps);
    return FALSE;
  } else if (size < info.size) {
    GST_ERROR_OBJECT (pool, "Provided size is to small for the caps: %u < %"
        G_GSIZE_FORMAT, size, info.size);
    return FALSE;
  }

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params)) {
    GST_ERROR_OBJECT (vpool, "Allocator missing from configuration!");
    return FALSE;
  } else if (NULL == allocator) {
    // No allocator set in configuration, create default FD allocator.
    if (NULL == (allocator = gst_fd_allocator_new ())) {
      GST_ERROR_OBJECT (vpool, "Failed to create FD allocator!");
      return FALSE;
    }
  }

  GST_DEBUG_OBJECT (pool, "Video dimensions %dx%d, caps %" GST_PTR_FORMAT,
      info.width, info.height, caps);

  priv->params = params;
  info.size = MAX (size, info.size);
  priv->info = info;

  // Check whether we should keep buffer memory mapped.
  priv->keepmapped = gst_buffer_pool_config_has_option (config,
      GST_IMAGE_BUFFER_POOL_OPTION_KEEP_MAPPED);

  // Remove cached allocator.
  if (priv->allocator)
    gst_object_unref (priv->allocator);

  priv->allocator = allocator;
  gst_object_ref (priv->allocator);

  // Enable metadata based on configuration of the pool.
  priv->addmeta = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  gst_buffer_pool_config_set_params (config, caps, priv->info.size, minbuffers,
      maxbuffers);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);
}

static GstFlowReturn
gst_image_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstImageBufferPool *vpool = GST_IMAGE_BUFFER_POOL (pool);
  GstImageBufferPoolPrivate *priv = vpool->priv;
  GstVideoInfo *info = &priv->info;
  GstMemory *memory = NULL;
  GstBuffer *newbuffer = NULL;

  memory = gst_allocator_alloc(priv->allocator,
                               GST_VIDEO_INFO_SIZE(&priv->info), NULL);

  if (NULL == memory) {
    GST_WARNING_OBJECT (pool, "Failed to allocate memory!");
    return GST_FLOW_ERROR;
  }

  // Create a GstBuffer.
  newbuffer = gst_buffer_new ();

  // Append the FD backed memory to the newly created GstBuffer.
  gst_buffer_append_memory(newbuffer, memory);

  if (priv->addmeta) {
    GST_DEBUG_OBJECT (vpool, "Adding GstVideoMeta");

    gst_buffer_add_video_meta_full (
        newbuffer, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_INFO_FORMAT(info),
        GST_VIDEO_INFO_WIDTH(info), GST_VIDEO_INFO_HEIGHT(info),
        GST_VIDEO_INFO_N_PLANES(info), info->offset, info->stride
    );
  }

  *buffer = newbuffer;
  return GST_FLOW_OK;
}

static void
gst_image_buffer_pool_free (GstBufferPool * pool, GstBuffer * buffer)
{
  gst_buffer_unref (buffer);
}

static void
gst_image_buffer_pool_reset (GstBufferPool * pool, GstBuffer * buffer)
{
  GstImageBufferPoolPrivate *priv = GST_IMAGE_BUFFER_POOL (pool)->priv;

  // Resize the buffer to the original size otherwise it will be discarded
  // due to the mismatch during the default implementation of release_buffer.
  gst_buffer_resize (buffer, 0, priv->info.size);

  GST_BUFFER_POOL_CLASS (parent_class)->reset_buffer (pool, buffer);
}

static void
gst_image_buffer_pool_finalize (GObject * object)
{
  GstImageBufferPool *vpool = GST_IMAGE_BUFFER_POOL (object);
  GstImageBufferPoolPrivate *priv = vpool->priv;

  GST_INFO_OBJECT (vpool, "Finalize video buffer pool %p", vpool);

  if (priv->allocator) {
    GST_INFO_OBJECT (vpool, "Free buffer pool allocator %p", priv->allocator);
    gst_object_unref (priv->allocator);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_image_buffer_pool_class_init (GstImageBufferPoolClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *pool = GST_BUFFER_POOL_CLASS (klass);

  object->finalize = gst_image_buffer_pool_finalize;

  pool->get_options = gst_image_buffer_pool_get_options;
  pool->set_config = gst_image_buffer_pool_set_config;
  pool->alloc_buffer = gst_image_buffer_pool_alloc;
  pool->free_buffer = gst_image_buffer_pool_free;
  pool->reset_buffer = gst_image_buffer_pool_reset;

  GST_DEBUG_CATEGORY_INIT (gst_image_pool_debug, "image-pool", 0,
      "image-pool object");
}

static void
gst_image_buffer_pool_init (GstImageBufferPool * vpool)
{
  vpool->priv = gst_image_buffer_pool_get_instance_private (vpool);
}


GstBufferPool *
gst_image_buffer_pool_new (const gchar * type)
{
  GstImageBufferPool *vpool;
  gboolean success = FALSE;

  vpool = g_object_new (GST_TYPE_IMAGE_BUFFER_POOL, NULL);

  vpool->priv->memtype = g_quark_from_string (type);

  if (GST_IS_SYSTEM_MEMORY_TYPE (vpool->priv->memtype)) {
    GST_INFO_OBJECT (vpool, "Using SYSTEM memory");
    success = TRUE;
  } else {
    GST_ERROR_OBJECT (vpool, "Invalid memory type %s!",
        g_quark_to_string (vpool->priv->memtype));
    success = FALSE;
  }

  if (!success) {
    gst_object_unref (vpool);
    return NULL;
  }

  GST_INFO_OBJECT (vpool, "New video buffer pool %p", vpool);
  return GST_BUFFER_POOL_CAST (vpool);
}

const GstVideoInfo *
gst_image_buffer_pool_get_info (GstBufferPool * pool)
{
  GstImageBufferPool *vpool = GST_IMAGE_BUFFER_POOL (pool);

  g_return_val_if_fail (vpool != NULL, NULL);

  return &vpool->priv->info;
}
