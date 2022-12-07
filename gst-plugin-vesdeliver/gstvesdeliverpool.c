// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <dlfcn.h>
#include <ion/ion.h>
#include <linux/msm_ion.h>
#include "gstvesdeliverpool.h"

/* Dynamically load libs by dlopen. */
static const char *ion_lib_name  = "libion.so.0";

GST_DEBUG_CATEGORY_EXTERN (vesdeliver_debug);
#define GST_CAT_DEFAULT vesdeliver_debug

#define gst_vesdeliver_allocator_parent_class parent_class
G_DEFINE_TYPE (GstVesDeliverAllocator, gst_vesdeliver_allocator,
    GST_TYPE_DMABUF_ALLOCATOR);

#define ALIGN(num, to) (((num) + (to - 1)) & (~(to - 1)))

static void
gst_vesdeliver_allocator_init (GstVesDeliverAllocator *alloc)
{
  GstAllocator *allocator = GST_ALLOCATOR_CAST (alloc);
  allocator->mem_type = GST_ALLOCATOR_VESDELIVER;
  GST_OBJECT_FLAG_SET (alloc, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);

  alloc->ion_handle = dlopen (ion_lib_name, RTLD_NOW);
  if (NULL == alloc->ion_handle) {
    const char *dlerr = dlerror();
    if (NULL == dlerr)
        dlerr = "NULL";
    GST_ERROR ("dlopen %s error: %s", ion_lib_name, dlerr);
    return;
  }

  alloc->ion_open = dlsym (alloc->ion_handle, "ion_open");
  alloc->ion_close = dlsym (alloc->ion_handle, "ion_close");
  alloc->ion_alloc_fd = dlsym (alloc->ion_handle, "ion_alloc_fd");

  if (!alloc->ion_open || !alloc->ion_close || !alloc->ion_alloc_fd) {
    GST_ERROR ("dlsym failed with ion_open: %p, ion_close: %p, ion_alloc_fd: %p",
          alloc->ion_open, alloc->ion_close, alloc->ion_alloc_fd);
    dlclose (alloc->ion_handle);
    alloc->ion_handle = NULL;
    return;
  } else {
    GST_INFO_OBJECT (alloc, "open %s(%p) successfully", ion_lib_name, alloc->ion_handle);
  }

  alloc->ion_fd = alloc->ion_open();
  if (alloc->ion_fd < 0) {
    GST_ERROR ("Open ION device failed with %d", alloc->ion_fd);
    return;
  }
}

GstAllocator *
gst_vesdeliver_allocator_new (gboolean secure)
{
  GstVesDeliverAllocator *allocator = NULL;

  allocator = (GstVesDeliverAllocator *) g_object_new (GST_TYPE_VESDELIVER_ALLOCATOR, NULL);
  if (NULL == allocator) {
    GST_ERROR ("Failed to create vesdeliver allocator");
    return NULL;
  }
  allocator->secure = secure;

  GST_INFO_OBJECT (allocator, "Create vesdeliver %s allocator %p",
    allocator->secure ? "secure":"normal", allocator);

  return GST_ALLOCATOR_CAST (allocator);
}

void
gst_vesdeliver_allocator_finalize (GObject *object)
{
  GstVesDeliverAllocator* allocator = GST_VESDELIVER_ALLOCATOR_CAST(object);
  if (allocator->ion_fd > 0) {
    allocator->ion_close (allocator->ion_fd);
    allocator->ion_fd = -1;
  }

  if (allocator->ion_handle) {
    GST_INFO_OBJECT (allocator, "dlclose %s(%p)", ion_lib_name, allocator->ion_handle);
    dlclose (allocator->ion_handle);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstMemory *
gst_vesdeliver_allocator_alloc (GstAllocator *allocator, gsize size,
    GstAllocationParams *params)
{
  GstVesDeliverAllocator *alloc = GST_VESDELIVER_ALLOCATOR (allocator);
  int rc = -EINVAL;
  GstMemory *mem = NULL;
  guint flags = 0;
  guint heap_mask = ION_HEAP (ION_SYSTEM_HEAP_ID);
  gint buf_fd = -1;
  gsize alloc_size = ALIGN (size, 4096);

  if (alloc->secure) {
    flags = ION_FLAG_SECURE | ION_FLAG_CP_BITSTREAM;
    heap_mask = ION_HEAP (ION_SECURE_HEAP_ID) | ION_HEAP (ION_SECURE_DISPLAY_HEAP_ID);
  }

  rc = alloc->ion_alloc_fd (alloc->ion_fd, alloc_size, 0, heap_mask, flags, &buf_fd);

  if (rc || buf_fd < 0) {
    GST_ERROR ("ion_alloc_fd failed with rc = %d", rc);
    return NULL;
  }

  mem = gst_dmabuf_allocator_alloc (allocator, buf_fd, alloc_size);
  if (mem) {
    GST_INFO_OBJECT (alloc, "Allocate %s gstmemory with size = %" G_GSIZE_FORMAT ", fd = %d",
        alloc->secure ? "secure" : "normal", alloc_size, buf_fd);
  }

  return mem;
}

static void
gst_vesdeliver_allocator_class_init (GstVesDeliverAllocatorClass *klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);

  obj_class->finalize = GST_DEBUG_FUNCPTR (gst_vesdeliver_allocator_finalize);
  allocator_class->alloc = GST_DEBUG_FUNCPTR (gst_vesdeliver_allocator_alloc);
}
