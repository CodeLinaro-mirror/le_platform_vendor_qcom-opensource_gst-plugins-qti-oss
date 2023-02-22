// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_VESDELIVER_H__
#define __GST_VESDELIVER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS
#define COMMON_VIDEO_CAPS(min, max) \
    "width = (int) [" #min ", " #max "], "    \
    "height = (int) [" #min ", " #max "]"
#define H264_CAPS \
    "video/x-h264, " \
    "stream-format = (string) { byte-stream }, " \
    "alignment = (string) { au }, " \
    COMMON_VIDEO_CAPS(96, 8192)
#define H265_CAPS \
    "video/x-h265, " \
    "stream-format = (string) { byte-stream }, " \
    "alignment = (string) { au }, " \
    COMMON_VIDEO_CAPS(96, 8192)
#define VP9_CAPS \
    "video/x-vp9, " \
    COMMON_VIDEO_CAPS(96, 4096)
#define MPEG2_CAPS \
    "video/mpeg, " \
    "mpegversion = (int)2, " \
    "parsed = (boolean)true, " \
    COMMON_VIDEO_CAPS(96, 1920)

#define GST_TYPE_VESDELIVER \
  (gst_vesdeliver_get_type())
#define GST_VESDELIVER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VESDELIVER,GstVesDeliver))
#define GST_VESDELIVER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VESDELIVER,GstVesDeliverClass))
#define GST_IS_VESDELIVER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VESDELIVER))
#define GST_IS_VESDELIVER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VESDELIVER))

typedef struct _GstVesDeliver GstVesDeliver;
typedef struct _GstVesDeliverClass GstVesDeliverClass;

typedef int (*Content_Protection_Set_AppName_Func) (const char *name);
typedef int (*Content_Protection_Copy_Init_Func) (void **p_handle);
typedef int (*Content_Protection_Copy_Func) (void *handle,
      uint8_t *non_sec_buf, uint32_t non_sec_buf_len, uint32_t sec_buf_fd,
      uint32_t sec_buf_offset, uint32_t *sec_buf_len, int copy_dir);
typedef int (*Content_Protection_Copy_Terminate_Func) (void **p_handle);

struct _GstVesDeliver
{
  GstBaseTransform parent;
  GstAllocator* allocator;
  gboolean secure;
  void *secure_handle;
  void *crypto_handle;
  Content_Protection_Set_AppName_Func Content_Protection_Set_AppName;
  Content_Protection_Copy_Init_Func Content_Protection_Copy_Init;
  Content_Protection_Copy_Func Content_Protection_Copy;
  Content_Protection_Copy_Terminate_Func Content_Protection_Copy_Terminate;
};

struct _GstVesDeliverClass
{
  GstBaseTransformClass parent_class;
};

GType gst_vesdeliver_get_type (void);

G_END_DECLS

#endif /* __GST_VESDELIVER_H__ */
