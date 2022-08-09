// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_QVDEINTERLACE_H__
#define __GST_QVDEINTERLACE_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS
#define GST_TYPE_QVDEINTERLACE (gst_qvdeinterlace_get_type())
G_DECLARE_FINAL_TYPE (GstQvdeinterlace, gst_qvdeinterlace,
    GST, QVDEINTERLACE, GstVideoFilter)

//#define GST_IS_QVDEINTERLACE_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_QVDEINTERLACE))
//#define GST_QVDEINTERLACE_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_QVDEINTERLACE, GstQvdeinterlaceClass))
//#define GST_QVDEINTERLACE_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_QVDEINTERLACE, GstQvdeinterlaceClass))

struct _GstQvdeinterlace {
  GstVideoFilter parent;

  /* gstbasetransform manages lifecycle of buffer pool totally.
   * use pool's aligned info to set up gpudi. */
  //GstBufferPool *pool; // not need this for it's stored in GstBaseTransform

  /* here are aligned info, info of caps are in GstVideoFilter */
  GstVideoInfo in_info;
  GstVideoInfo out_info;

  /* GPU deinterlace handle */
  int gpudi_handle;
  /* GPU deinterlace reference buffer held */
  GstBuffer *ref_buf_held;

  gboolean active;
  gboolean silent;

  gboolean in_dmabuf;
  gboolean out_dmabuf;

  gboolean in_ubwc;
  gboolean out_ubwc;
};

G_END_DECLS
#endif /* __GST_QVDEINTERLACE_H__ */
