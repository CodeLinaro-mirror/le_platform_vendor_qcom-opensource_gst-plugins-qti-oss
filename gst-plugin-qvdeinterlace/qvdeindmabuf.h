// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GBM_DMABUF_H__
#define __GBM_DMABUF_H__

#include <gst/gst.h>
#include <gst/video/video.h>

/* DmaBufDesc is opaque to user. */
#ifdef USE_GBM
typedef struct gbm_buf_desc DmaBufDesc;
#else
typedef struct dma_heap_allocation_data DmaBufDesc;
#endif

gboolean qvdein_dmabuf_alloc (DmaBufDesc ** desc,
    const GstVideoInfo * info, gboolean ubwc);

gint qvdein_dmabuf_get_fd (const DmaBufDesc * desc);

gsize qvdein_dmabuf_get_size (const DmaBufDesc * desc);

guint64 qvdein_dmabuf_get_modifier (const DmaBufDesc * desc);

void qvdein_dmabuf_align_info (const DmaBufDesc * desc, GstVideoInfo * info);

void qvdein_dmabuf_free (DmaBufDesc * desc);

#endif /* __GBM_DMABUF_H__ */
