// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_GPUDEINTERLACE_H__
#define __GST_GPUDEINTERLACE_H__

#ifdef USE_GPU_DEINTERLACE
#include <gpudi.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

typedef enum
{
  GPUDI_SCAN_METHOD_NONE = -1,
  GPUDI_SCAN_METHOD_PROGRESSIVE = 0,
  GPUDI_SCAN_METHOD_TOP_FIRST = 1,
  GPUDI_SCAN_METHOD_BOTTOM_FIRST = 2,
} GpudiScanMethod;

#ifdef USE_GPU_DEINTERLACE
/* GpudiBufDesc is opaque to user. */
typedef GpuDeinterlace_BufferDesc GpudiBufDesc;
#else
/* It's meaningless just for passing compilation. */
typedef gint GpudiBufDesc;
#endif /* USE_GPU_DEINTERLACE */

void
gpu_deinterlace_fill_desc (GpudiBufDesc * desc, const GstVideoInfo * info,
    gint fd, gboolean ubwc, GpudiScanMethod scan);

static inline gint
gpu_deinterlace_invalidate_reference_option (gboolean invalidate)
{
  gint option = 0;

#ifdef USE_GPU_DEINTERLACE
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

gint gpu_deinterlace_reset_instance (gint handle);

gint gpu_deinterlace_close_instance (gint handle);

#endif /* __GST_GPUDEINTERLACE_H__ */
