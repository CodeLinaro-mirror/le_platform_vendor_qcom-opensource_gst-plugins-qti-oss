// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef _GST_VESDELIVER_ALLOCATOR_H_
#define _GST_VESDELIVER_ALLOCATOR_H_

#include <stdint.h>
#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#ifdef USE_DMAHEAP
#include <BufferAllocator/BufferAllocatorWrapper.h>
#include <vmmem_wrapper.h>
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
typedef struct _BitstreamBuffer BitstreamBuffer;
typedef struct _AllocatorParameter AllocatorParameter;

#ifdef USE_DMAHEAP
typedef BufferAllocator *(*create_allocator_func) ();
typedef void (*free_allocator_func) (BufferAllocator * buffer_allocator);
typedef int (*alloc_func) (BufferAllocator * buffer_allocator,
    const char *heap_name, size_t len, unsigned int heap_flags,
    size_t legacy_align);
typedef int (*ReclaimDmabuf_Func) (VmMem * instance, int dma_buf_fd,
    int64_t memparcel_hdl);
#else
typedef int (*ion_open_func) (void);
typedef int (*ion_close_func) (int fd);
typedef int (*ion_alloc_fd_func) (int fd, size_t len, size_t align,
    unsigned int heap_mask, unsigned int flags, int *handle_fd);
#endif

typedef enum
{
  SECURE_DISABLE,
  SECURE_COPY,
  LEND_DMABUF,
} SECURE_MODE;

struct _AllocatorParameter
{
  SECURE_MODE secure_mode;
  gboolean buf_recycle;
  gboolean buf_contiguous;
  guint threshold_buf_count;
#ifdef USE_DMAHEAP
  VmMem *vm_instance;
  ReclaimDmabuf_Func ReclaimDmabuf;
#endif
};

struct _BitstreamBuffer
{
  gboolean used;
  gsize size;
  gint fd;
  GstMemory *mem;
};

struct _GstVesDeliverAllocator
{
  GstDmaBufAllocator parent;
  GMutex buf_lock;
  GCond buf_cond;
  GSList *buffer_list;
  gsize max_alloc_buf_size;
  AllocatorParameter param;

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

struct _GstVesDeliverAllocatorClass
{
  GstDmaBufAllocatorClass parent_class;
};

GType gst_vesdeliver_allocator_get_type (void);
GstAllocator *gst_vesdeliver_allocator_new (AllocatorParameter * param);

G_END_DECLS
#endif /* _GST_VESDELIVER_ALLOCATOR_H_ */
