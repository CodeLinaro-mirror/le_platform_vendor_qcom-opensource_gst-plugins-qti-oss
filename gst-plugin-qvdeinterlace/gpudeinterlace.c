// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gpudeinterlace.h"

#include <gst/gstinfo.h>

GST_DEBUG_CATEGORY_EXTERN (gst_qvdeinterlace_debug);
#define GST_CAT_DEFAULT gst_qvdeinterlace_debug

void
gpu_deinterlace_fill_desc (GpudiBufDesc * desc, const GstVideoInfo * info,
    gint fd, gboolean ubwc, GpudiScanMethod scan)
{
  GST_DEBUG ("desc %p, info %p, fd %d, ubwc %d, scan %d",
      desc, info, fd, ubwc, scan);
}

/* return a GPU deinterlace instance handle */
gint
gpu_deinterlace_open_instance (const GpudiBufDesc * dst,
    const GpudiBufDesc * src)
{
  gint handle = 0;

  GST_DEBUG ("handle %d", handle);

  return handle;
}

/* 2 cases of seeking & field order change need to set flags bit to
 * invalidate reference frame, gpudi does bob for 1st new frame */
gint
gpu_deinterlace_process_frame (gint handle, GpudiBufDesc * dst,
    const GpudiBufDesc * src, gint options)
{
  GST_DEBUG ("handle %d, dst %p, src %p, options 0x%x",
      handle, dst, src, options);

  return 0;
}

/* in case of resolution change, must do reset by this firstly, and close
 * old dmabuf fd set, then reconfigure and allocate new dmabuf fd set */
gint
gpu_deinterlace_reset_instance (gint handle)
{
  GST_DEBUG ("handle %d", handle);

  return 0;
}

gint
gpu_deinterlace_close_instance (gint handle)
{
  GST_DEBUG ("handle %d", handle);

  return 0;
}
