// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef _GST_VESDELIVER_POOL_H_
#define _GST_VESDELIVER_POOL_H_

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideopool.h>
#include <gst/allocators/allocators.h>

G_BEGIN_DECLS

#define GST_TYPE_VESDELIVER_BUFFER_POOL \
  (gst_vesdeliver_buffer_pool_get_type())
#define GST_VESDELIVER_BUFFER_POOL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VESDELIVER_BUFFER_POOL, GstVesDeliverBufferPool))
#define GST_IS_VESDELIVER_BUFFER_POOL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VESDELIVER_BUFFER_POOL))
#define GST_VESDELIVER_BUFFER_POOL_CAST(obj) ((GstVesDeliverBufferPool*)(obj))

typedef struct _GstVesDeliverBufferPool GstVesDeliverBufferPool;
typedef struct _GstVesDeliverBufferPoolClass GstVesDeliverBufferPoolClass;

struct _GstVesDeliverBufferPool
{
  GstBufferPool bufferpool;
  GstAllocator *allocator;
};

struct _GstVesDeliverBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GType gst_vesdeliver_buffer_pool_get_type (void);
GstBufferPool *gst_vesdeliver_buffer_pool_new ();

G_END_DECLS

#endif /* _GST_VESDELIVER_POOL_H_ */
