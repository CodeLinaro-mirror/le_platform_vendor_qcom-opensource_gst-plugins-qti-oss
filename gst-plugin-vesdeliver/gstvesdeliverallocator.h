// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef _GST_VESDELIVER_ALLOCATOR_H_
#define _GST_VESDELIVER_ALLOCATOR_H_

#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#ifdef USE_DMAHEAP
#include <BufferAllocator/BufferAllocatorWrapper.h>
#else
#include <ion/ion.h>
#include <linux/msm_ion.h>
#endif

G_BEGIN_DECLS

#define GST_ALLOCATOR_VESDELIVER "vesdeliver_allocator"

#define GST_TYPE_VESDELIVER_ALLOCATOR \
  (gst_vesdeliver_allocator_get_type())
#define GST_VESDELIVER_ALLOCATOR_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_VESDELIVER_ALLOCATOR, GstVesDeliverAllocatorClass))
#define GST_VESDELIVER_ALLOCATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_VESDELIVER_ALLOCATOR, GstVesDeliverAllocator))
#define GST_VESDELIVER_ALLOCATOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_VESDELIVER_ALLOCATOR, GstVesDeliverAllocatorClass))
#define GST_VESDELIVER_ALLOCATOR_CAST(obj) ((GstVesDeliverAllocator *) (obj))

typedef struct _GstVesDeliverAllocator GstVesDeliverAllocator;
typedef struct _GstVesDeliverAllocatorClass GstVesDeliverAllocatorClass;

#ifdef USE_DMAHEAP
typedef BufferAllocator* (*create_allocator_func)();
typedef void (*free_allocator_func) (BufferAllocator* buffer_allocator);
typedef int (*alloc_func) (BufferAllocator* buffer_allocator, const char* heap_name, size_t len,
                    unsigned int heap_flags, size_t legacy_align);
#else
typedef int (*ion_open_func) (void);
typedef int (*ion_close_func) (int fd);
typedef int (*ion_alloc_fd_func) (int fd, size_t len, size_t align, unsigned int heap_mask,
      unsigned int flags, int *handle_fd);
#endif

struct _GstVesDeliverAllocator {
  GstDmaBufAllocator parent;
  gboolean secure;

  void *lib_handle;
#ifdef USE_DMAHEAP
  BufferAllocator *dmaheap_allocator;
  create_allocator_func create_allocator;
  free_allocator_func free_allocator;
  alloc_func alloc_fd;
#else
  int ion_fd;
  ion_open_func ion_open;
  ion_close_func ion_close;
  ion_alloc_fd_func ion_alloc_fd;
#endif
};

struct _GstVesDeliverAllocatorClass {
  GstDmaBufAllocatorClass parent_class;
};

GType gst_vesdeliver_allocator_get_type (void);
GstAllocator *gst_vesdeliver_allocator_new (gboolean secure);

G_END_DECLS

#endif /* _GST_VESDELIVER_ALLOCATOR_H_ */
