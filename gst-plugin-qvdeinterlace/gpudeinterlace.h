// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_GPUDEINTERLACE_H__
#define __GST_GPUDEINTERLACE_H__

//#define QTI_PLATFORM

#ifdef QTI_PLATFORM
#include <gpudi.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

typedef enum {
  GPUDI_SCAN_METHOD_NONE = -1,
  GPUDI_SCAN_METHOD_PROGRESSIVE = 0,
  GPUDI_SCAN_METHOD_TOP_FIRST,
  GPUDI_SCAN_METHOD_BOTTOM_FIRST,
} GpudiScanMethod;

/* This is opaque to client, do NOT use the fields directly. */
struct gpudi_buf_desc
{
  gint fd;
  GstVideoFormat format;
  gint width;
  gint height;
  gint stride;
  gsize size;
  gboolean ubwc;
  GpudiScanMethod scan;
};

/* GpudiBufDesc is opaque to client */
typedef struct gpudi_buf_desc GpudiBufDesc;

void
gpu_deinterlace_fill_desc (GpudiBufDesc * desc, const GstVideoInfo * info,
    gint fd, gboolean ubwc, GpudiScanMethod scan);

static inline gint
gpu_deinterlace_invalidate_reference_option (gboolean invalidate)
{
  gint option = 0;

#ifdef QTI_PLATFORM
  if (invalidate)
    option = GpuDeinterlace_Invalidate_Backward_Reference;
#endif

  return option;
}

gint
gpu_deinterlace_open_instance (const GpudiBufDesc * dst,
    const GpudiBufDesc * src);

gint
gpu_deinterlace_process_frame (gint handle, GpudiBufDesc * dst,
    const GpudiBufDesc * src, gint flags);

gint
gpu_deinterlace_reset_instance (gint handle);

gint
gpu_deinterlace_close_instance (gint handle);

#endif /* __GST_GPUDEINTERLACE_H__ */