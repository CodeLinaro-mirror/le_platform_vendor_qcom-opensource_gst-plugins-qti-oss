// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __GST_VESDELIVER_H__
#define __GST_VESDELIVER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstbasetransform.h>

// these IDs are fixed worldwide, see https://dashif.org/identifiers/content_protection/
#define PLAYREADY_PROTECTION_SYSTEM_ID "9a04f079-9840-4286-ab92-e65be0885f95"
#define WIDEVINE_PROTECTION_SYSTEM_ID  "edef8ba9-79d6-4ace-a3c8-27dcd51d21ed"

G_BEGIN_DECLS
#define COMMON_VIDEO_CAPS(min, max) \
    "width = (int) [" #min ", " #max "], "    \
    "height = (int) [" #min ", " #max "]"
#define H264_CAPS \
    "video/x-h264, " \
    "stream-format = (string) { byte-stream, avc }, " \
    "alignment = (string) { au }, " \
    COMMON_VIDEO_CAPS(96, 8192)
#define H265_CAPS \
    "video/x-h265, " \
    "stream-format = (string) { byte-stream, hvc1, hev1 }, " \
    "alignment = (string) { au }, " \
    COMMON_VIDEO_CAPS(96, 8192)
#define VP8_CAPS \
    "video/x-vp8, " \
    COMMON_VIDEO_CAPS(96, 4096)
#define VP9_CAPS \
    "video/x-vp9, " \
    COMMON_VIDEO_CAPS(96, 4096)
#define MPEG2_CAPS \
    "video/mpeg, " \
    "mpegversion = (int)2, " \
    "parsed = (boolean)true, " \
    COMMON_VIDEO_CAPS(96, 1920)
#define AV1_CAPS \
    "video/x-av1, " \
    COMMON_VIDEO_CAPS(96, 8192)
#define PLAYREADY_CENC_H264_CAPS \
    "application/x-cenc, " \
    "original-media-type=(string)video/x-h264, " \
    "protection-system = (string) " PLAYREADY_PROTECTION_SYSTEM_ID
#define PLAYREADY_CENC_H265_CAPS \
    "application/x-cenc, " \
    "original-media-type=(string)video/x-h265, " \
    "protection-system = (string) " PLAYREADY_PROTECTION_SYSTEM_ID
#define WIDEVINE_CENC_H264_CAPS \
    "application/x-cenc, " \
    "original-media-type=(string)video/x-h264, " \
    "protection-system = (string) " WIDEVINE_PROTECTION_SYSTEM_ID
#define WIDEVINE_CENC_H265_CAPS \
    "application/x-cenc, " \
    "original-media-type=(string)video/x-h265, " \
    "protection-system = (string) " WIDEVINE_PROTECTION_SYSTEM_ID
#define PLAYREADY_CENC_VP9_CAPS \
    "application/x-cenc, " \
    "original-media-type=(string)video/x-vp9, " \
    "protection-system = (string) " PLAYREADY_PROTECTION_SYSTEM_ID
#define WIDEVINE_CENC_VP9_CAPS \
    "application/x-cenc, " \
    "original-media-type=(string)video/x-vp9, " \
    "protection-system = (string) " WIDEVINE_PROTECTION_SYSTEM_ID
#define PLAYREADY_CENC_AV1_CAPS \
    "application/x-cenc, " \
    "original-media-type=(string)video/x-av1, " \
    "protection-system = (string) " PLAYREADY_PROTECTION_SYSTEM_ID
#define WIDEVINE_CENC_AV1_CAPS \
    "application/x-cenc, " \
    "original-media-type=(string)video/x-av1, " \
    "protection-system = (string) " WIDEVINE_PROTECTION_SYSTEM_ID
#define VIDEO_RAW_CAPS \
    "video/x-raw"
#define VIDEO_RAW_DMABUF_CAPS \
    "video/x-raw(memory:DMABuf)"
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

#ifdef USE_DMAHEAP
typedef VmMem *(*CreateVmMem_Func) (void);
typedef void (*FreeVmMem_Func) (VmMem * instance);
typedef int (*IsExclusiveOwnerDmabuf_Func) (int fd, bool * is_exclusive_owner);
typedef VmHandle (*FindVmByName_Func) (VmMem * instance, char *cstr);
typedef int (*LendDmabuf_Func) (VmMem * instance, int dma_buf_fd,
    VmHandle * handles, uint32_t * perms, int nr);
#endif
typedef int (*Content_Protection_Set_AppName_Func) (const char *name);
typedef int (*Content_Protection_Copy_Init_Func) (void **p_handle);
typedef int (*Content_Protection_Copy_Func) (void *handle,
    uint8_t * non_sec_buf, uint32_t non_sec_buf_len, uint32_t sec_buf_fd,
    uint32_t sec_buf_offset, uint32_t * sec_buf_len, int copy_dir);
typedef int (*Content_Protection_Copy_Terminate_Func) (void **p_handle);

typedef enum
{
  TRANSFORM_DISABLE,
  TRANSFORM_CENC_TO_CLEAR,
  TRANSFORM_CLEAR_TO_CENC,
  TRANSFORM_RAWVIDEODMA_TO_RAWVIDEO,
} TRANSFORM_CAPS;

struct _GstVesDeliver
{
  GstBaseTransform parent;
  GstAllocator *allocator;
  SECURE_MODE secure;
  gboolean buf_recycle;
  gboolean buf_contiguous;
  TRANSFORM_CAPS transform_caps;
  void *secure_handle;
  void *crypto_handle;
  gint min_output_buf_size;
  gchar *input_format;
  gint input_width;
  gint input_height;
#ifdef USE_DMAHEAP
  void *vmmem_lib_handle;
  VmMem *vm_instance;
  VmHandle vm_handle;
  CreateVmMem_Func CreateVmMem;
  FreeVmMem_Func FreeVmMem;
  IsExclusiveOwnerDmabuf_Func IsExclusiveOwnerDmabuf;
  FindVmByName_Func FindVmByName;
  LendDmabuf_Func LendDmabuf;
  ReclaimDmabuf_Func ReclaimDmabuf;
#endif
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
