// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_GSTQVDEINPOOL_H__
#define __GST_GSTQVDEINPOOL_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS
#define GST_TYPE_QVDEIN_POOL (gst_qvdein_pool_get_type())
G_DECLARE_FINAL_TYPE (GstQvdeinPool, gst_qvdein_pool,
    GST, QVDEIN_POOL, GstBufferPool)

struct _GstQvdeinPool {
  GstBufferPool parent;

  GstVideoInfo info;
  GstVideoInfo aligned_info;

  GstAllocator *allocator;
  GstAllocationParams params;

  gboolean ubwc;
  gboolean done_align_info;
};

GstBufferPool * gst_qvdein_pool_new (gboolean ubwc);

/* only can get aligned info after first allocation */
static inline GstVideoInfo *
gst_qvdein_pool_aligned_info (const GstBufferPool * pool)
{
  GstQvdeinPool *self = GST_QVDEIN_POOL ((GstBufferPool *) pool);

  return &self->aligned_info;
}

gint
gst_qvdein_pool_buffer_get_fd (const GstBufferPool * pool,
    const GstBuffer * buffer);

gboolean
gst_qvdein_pool_buffer_get_ubwc (const GstBufferPool * pool,
    const GstBuffer * buffer);

G_END_DECLS
#endif /* __GST_GSTQVDEINPOOL_H__ */
