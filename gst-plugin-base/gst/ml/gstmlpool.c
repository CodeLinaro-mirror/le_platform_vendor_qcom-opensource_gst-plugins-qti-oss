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

#include "gstmlpool.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "gstmlmeta.h"

#define ml_unused(name) (void)name;

GST_DEBUG_CATEGORY_STATIC (gst_ml_pool_debug);
#define GST_CAT_DEFAULT gst_ml_pool_debug

#define GST_IS_SYSTEM_MEMORY_TYPE(type) \
    (type == g_quark_from_static_string (GST_ML_BUFFER_POOL_TYPE_SYSTEM))

struct _GstMLBufferPoolPrivate
{
  // Allocation memory type.
  GQuark memtype;

  GstAllocator *allocator;
  GstAllocationParams params;
  GstMLInfo info;

  gboolean addmeta;
  gboolean continuous;
};

#define gst_ml_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstMLBufferPool, gst_ml_buffer_pool,
    GST_TYPE_BUFFER_POOL);

static const gchar **
gst_ml_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = {
    GST_ML_BUFFER_POOL_OPTION_TENSOR_META,
    GST_ML_BUFFER_POOL_OPTION_CONTINUOUS,
    NULL
  };
  return options;
}

static gboolean
gst_ml_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstMLBufferPool *mlpool = GST_ML_POOL (pool);
  GstMLBufferPoolPrivate *priv = mlpool->priv;

  GstCaps *caps;
  guint maxsize, minbuffers, maxbuffers;
  GstMLInfo info;
  GstAllocator *allocator;
  GstAllocationParams params;
  gboolean success;

  success = gst_buffer_pool_config_get_params (config, &caps, &maxsize,
      &minbuffers, &maxbuffers);

  if (!success) {
    GST_ERROR_OBJECT (mlpool, "Invalid configuration!");
    return FALSE;
  } else if (caps == NULL) {
    GST_ERROR_OBJECT (mlpool, "Caps missing from configuration");
    return FALSE;
  }

  // Now parse the caps from the configuration.
  if (!gst_ml_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (mlpool, "Failed getting geometry from caps %"
        GST_PTR_FORMAT, caps);
    return FALSE;
  } else if (maxsize != gst_ml_info_size (&info)) {
    GST_ERROR_OBJECT (pool, "Provided size is not equal for the caps: %u != %"
        G_GSIZE_FORMAT, maxsize, gst_ml_info_size (&info));
    return FALSE;
  }

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params)) {
    GST_ERROR_OBJECT (mlpool, "Allocator missing from configuration");
    return FALSE;
  } else if (GST_IS_SYSTEM_MEMORY_TYPE (priv->memtype) &&
      GST_IS_FD_ALLOCATOR (allocator)) {
    GST_ERROR_OBJECT (mlpool, "Allocator %p cannot be FD backed!", allocator);
    return FALSE;
  }

  GST_DEBUG_OBJECT (pool, "Caps %" GST_PTR_FORMAT, caps);

  priv->params = params;
  priv->info = info;

  // Remove cached allocator.
  if (priv->allocator)
    gst_object_unref (priv->allocator);

  priv->allocator = allocator;
  gst_object_ref (priv->allocator);

  // Enable metadata based on configuration of the pool.
  priv->addmeta = gst_buffer_pool_config_has_option (config,
      GST_ML_BUFFER_POOL_OPTION_TENSOR_META);
  // Enable allocation of continuous memory based on configuration of the pool.
  priv->continuous = gst_buffer_pool_config_has_option (config,
      GST_ML_BUFFER_POOL_OPTION_CONTINUOUS);

  gst_buffer_pool_config_set_params (config, caps, maxsize, minbuffers,
      maxbuffers);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);
}

static GstFlowReturn
gst_ml_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstMLBufferPool *mlpool = GST_ML_POOL (pool);
  GstMLBufferPoolPrivate *priv = mlpool->priv;
  GstMemory *mem = NULL;
  GstBuffer *newbuffer = NULL;
  GstMLTensorMeta *meta = NULL;
  guint idx = 0, size = 0;

  // Create a GstBuffer.
  newbuffer = gst_buffer_new ();

  for (idx = 0; idx < priv->info.n_tensors; idx++) {
    // Check if a single continuous memory block is requested for all tensors.
    size = priv->continuous ? gst_ml_info_size (&priv->info) :
        gst_ml_info_tensor_size (&priv->info, idx);

    if (GST_IS_SYSTEM_MEMORY_TYPE (priv->memtype))
      mem = gst_allocator_alloc (priv->allocator, size, NULL);

    if (NULL == mem) {
      GST_WARNING_OBJECT (mlpool, "Failed to allocate memory!");
      gst_buffer_unref (newbuffer);
      return GST_FLOW_ERROR;
    }
    // Append the memory block to the newly created GstBuffer.
    gst_buffer_append_memory (newbuffer, mem);

    // Break the loop as only one memory block is going to be allocated.
    if (priv->continuous) break;
  }

  // Reset the index counter.
  idx = 0;

  // Add tensor meta.
  while (priv->addmeta && (idx < priv->info.n_tensors)) {
    GST_DEBUG_OBJECT (mlpool, "Adding GstMLTensorMeta");

    meta = gst_buffer_add_ml_tensor_meta (newbuffer, priv->info.type,
        priv->info.n_dimensions[idx], priv->info.tensors[idx]);
    meta->id = idx;

    idx++;
  }

  *buffer = newbuffer;
  return GST_FLOW_OK;
}

static void
gst_ml_buffer_pool_free (GstBufferPool * pool, GstBuffer * buffer)
{
  GstMLBufferPool *mlpool = GST_ML_POOL (pool);
  ml_unused(mlpool);

  gst_buffer_unref (buffer);
}

static void
gst_ml_buffer_pool_finalize (GObject * object)
{
  GstMLBufferPool *mlpool = GST_ML_POOL (object);
  GstMLBufferPoolPrivate *priv = mlpool->priv;

  GST_INFO_OBJECT (mlpool, "Finalize ML buffer pool %p", mlpool);

  if (priv->allocator) {
    GST_INFO_OBJECT (mlpool, "Free buffer pool allocator %p", priv->allocator);
    gst_object_unref (priv->allocator);
  }
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ml_buffer_pool_class_init (GstMLBufferPoolClass * klass)
{
  GObjectClass *object = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *pool = GST_BUFFER_POOL_CLASS (klass);

  object->finalize = gst_ml_buffer_pool_finalize;

  pool->get_options = gst_ml_buffer_pool_get_options;
  pool->set_config = gst_ml_buffer_pool_set_config;
  pool->alloc_buffer = gst_ml_buffer_pool_alloc;
  pool->free_buffer = gst_ml_buffer_pool_free;

  GST_DEBUG_CATEGORY_INIT (gst_ml_pool_debug, "mlpool", 0, "ML Buffer Pool");
}

static void
gst_ml_buffer_pool_init (GstMLBufferPool * mlpool)
{
  mlpool->priv = gst_ml_buffer_pool_get_instance_private (mlpool);
}

GstBufferPool *
gst_ml_buffer_pool_new (const gchar * memtype)
{
  GstMLBufferPool *mlpool;
  gboolean success = FALSE;

  g_return_val_if_fail (memtype != NULL, NULL);

  mlpool = g_object_new (GST_TYPE_ML_POOL, NULL);

  mlpool->priv->memtype = g_quark_from_static_string (memtype);
  mlpool->priv->addmeta = FALSE;
  mlpool->priv->continuous = FALSE;

  if (GST_IS_SYSTEM_MEMORY_TYPE (mlpool->priv->memtype)) {
    GST_INFO_OBJECT (mlpool, "Using SYSTEM memory");
    success = TRUE;
  }
  else {
    GST_ERROR_OBJECT (mlpool, "Invalid memory type %s!",
        g_quark_to_string (mlpool->priv->memtype));
    success = FALSE;
  }

  if (!success) {
    gst_object_unref (mlpool);
    return NULL;
  }

  GST_INFO_OBJECT (mlpool, "New ML buffer pool %p", mlpool);
  return GST_BUFFER_POOL_CAST (mlpool);
}
