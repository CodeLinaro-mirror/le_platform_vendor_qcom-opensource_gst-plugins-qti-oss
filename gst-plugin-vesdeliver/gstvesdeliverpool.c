// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <dlfcn.h>
#include <ion/ion.h>
#include <linux/msm_ion.h>
#include "gstvesdeliverpool.h"

GST_DEBUG_CATEGORY_STATIC (gst_vesdeliver_pool_debug);
#define GST_CAT_DEFAULT gst_vesdeliver_pool_debug

#define gst_vesdeliver_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstVesDeliverBufferPool, gst_vesdeliver_buffer_pool,
    GST_TYPE_BUFFER_POOL);

/* Dynamically load libion by dlopen. */
static const char *ion_lib_name  = "libion.so.0";
static void* ion_lib = NULL;

static void
gst_vesdeliver_buffer_pool_init (GstVesDeliverBufferPool * pool)
{
  GST_DEBUG_CATEGORY_INIT (gst_vesdeliver_pool_debug,
      "vesdeliverpool", 0, "QTI GST vesdeliver buffer pool");
  pool->allocator = NULL;

  if (NULL == ion_lib) {
    ion_lib = dlopen (ion_lib_name, RTLD_NOW);
    GST_INFO_OBJECT (pool, "open ION lib: %p", ion_lib);
    if (ion_lib == NULL) {
      GST_ERROR ("dlopen libion.so failed");
      return;
    }
  }
}

static void
gst_vesdeliver_buffer_pool_finalize (GObject * object)
{
  GstVesDeliverBufferPool *pool = GST_VESDELIVER_BUFFER_POOL (object);

  GST_INFO_OBJECT (pool, "pool %p", pool);
  if (pool->allocator)
    gst_object_unref (pool->allocator);

  if (ion_lib) {
    dlclose (ion_lib);
    ion_lib = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vesdeliver_buffer_pool_class_init (GstVesDeliverBufferPoolClass * klass)
{
  GObjectClass *gobj_class = (GObjectClass *) klass;
  GstBufferPoolClass *bp_class = (GstBufferPoolClass *) klass;

  gobj_class->finalize = GST_DEBUG_FUNCPTR (gst_vesdeliver_buffer_pool_finalize);
}

GstBufferPool *
gst_vesdeliver_buffer_pool_new ()
{
  GstVesDeliverBufferPool *pool = NULL;

  pool = (GstVesDeliverBufferPool*)
      g_object_new (GST_TYPE_VESDELIVER_BUFFER_POOL, NULL);

  if (!pool) {
    GST_ERROR ("failed to create buffer pool");
    return NULL;
  }

  return GST_BUFFER_POOL (pool);
}
