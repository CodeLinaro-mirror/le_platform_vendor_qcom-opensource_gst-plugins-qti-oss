// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef _GST_VESDELIVER_ALLOCATOR_H_
#define _GST_VESDELIVER_ALLOCATOR_H_

#include <gst/gst.h>
#include <gst/allocators/allocators.h>

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

typedef int (*ion_open_func) (void);
typedef int (*ion_close_func) (int fd);
typedef int (*ion_alloc_fd_func) (int fd, size_t len, size_t align, unsigned int heap_mask,
      unsigned int flags, int *handle_fd);

struct _GstVesDeliverAllocator {
  GstDmaBufAllocator parent;
  gboolean secure;
  int ion_fd;
  void *ion_handle;
  ion_open_func ion_open;
  ion_close_func ion_close;
  ion_alloc_fd_func ion_alloc_fd;
};

struct _GstVesDeliverAllocatorClass {
  GstDmaBufAllocatorClass parent_class;
};

GType gst_vesdeliver_allocator_get_type (void);
GstAllocator *gst_vesdeliver_allocator_new (gboolean secure);

G_END_DECLS

#endif /* _GST_VESDELIVER_ALLOCATOR_H_ */
