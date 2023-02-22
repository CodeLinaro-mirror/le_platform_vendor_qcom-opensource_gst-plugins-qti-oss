/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/*
* Changes from Qualcomm Innovation Center are provided under the following license:

* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __GST_QCODEC2_VENC_H__
#define __GST_QCODEC2_VENC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>
#include <gst/video/gstvideopool.h>
#include <gst/allocators/allocators.h>
#include "gstqcodec2bufferpool.h"

#include "codec2wrapper.h"

G_BEGIN_DECLS
#define GST_TYPE_QCODEC2_VENC          (gst_qcodec2_venc_get_type())
#define GST_QCODEC2_VENC(obj)          (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QCODEC2_VENC,GstQcodec2Venc))
#define GST_QCODEC2_VENC_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QCODEC2_VENC,GstQcodec2VencClass))
#define GST_QCODEC2_VENC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_QCODEC2_VENC,GstQcodec2VencClass))
#define GST_IS_QCODEC2_VENC(obj)       (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QCODEC2_VENC))
#define GST_IS_QCODEC2_VENC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QCODEC2_VENC))
typedef struct _GstQcodec2Venc GstQcodec2Venc;
typedef struct _GstQcodec2VencClass GstQcodec2VencClass;

/* pad templates */
#define GST_QC2VENC_CAPS_MAKE(format,min,max) \
    "video/x-raw, "                           \
    "format = (string) " format ", "          \
    "width = (int) [" #min ", " #max "], "    \
    "height = (int) [" #min ", " #max "],"    \
    "framerate = " GST_VIDEO_FPS_RANGE

#define GST_QC2VENC_CAPS_MAKE_WITH_FEATURES(feature,format,min,max) \
    "video/x-raw(" feature "), "                                    \
    "format = (string) " format ", "                                \
    "width = (int) [" #min ", " #max "], "                          \
    "height = (int) [" #min ", " #max "],"                          \
    "framerate = " GST_VIDEO_FPS_RANGE

/* Maximum number of input frame queued */
#define MAX_QUEUED_FRAME  32

typedef struct
{
  const gchar *profile;
  C2W_PROFILE_T e;
} ProfileMapping;

typedef struct
{
  const gchar *level;
  C2W_LEVEL_T e;
} LevelMapping;

struct _GstQcodec2Venc
{
  GstVideoEncoder parent;

  /* Public properties */
  gboolean silent;

  void *comp_store;
  void *comp;
  void *comp_intf;
  gchar *comp_name;

  /* manage the lifetime of C2 component adapter */
  GstC2Comp *gst_c2_comp;

  guint64 queued_frame[MAX_QUEUED_FRAME];

  GstBufferPool *pool;
  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;

  gboolean input_setup;
  gboolean output_setup;
  gboolean eos_reached;

  gint width;
  gint height;
  GstVideoFormat input_format;
  GstVideoInfo input_info;
  guint64 frame_index;
  guint64 num_input_queued;
  guint64 num_output_done;

  GstVideoInterlaceMode interlace_mode;
  RC_MODE_TYPE rcMode;
  MIRROR_TYPE mirror;
  guint32 rotation;
  guint32 downscale_width;
  guint32 downscale_height;
  gboolean color_space_conversion;
  COLOR_PRIMARIES primaries;
  TRANSFER_CHAR transfer_char;
  MATRIX matrix;
  FULL_RANGE full_range;
  IR_MODE_TYPE intra_refresh_mode;
  guint32 intra_refresh_mbs;
  guint32 target_bitrate;
  guint32 configured_target_bitrate;
  SLICE_MODE slice_mode;
  guint32 slice_size;
  GMutex pending_lock;
  GCond pending_cond;
  BLUR_MODE blur_mode;
  guint32 blur_width;
  guint32 blur_height;
  gboolean is_ubwc;
  GArray *roi_array;
  char *roi_type;
  char *roi_rect_payload;
  char *roi_rect_payload_ext;
  BITRATE_SAVING_MODE bitrate_saving_mode;
  gboolean is_heic;
  guint32 interval_intraframes;
  gboolean inline_sps_pps_headers;
  guint32 min_qp_i_frames;
  guint32 max_qp_i_frames;
  guint32 min_qp_p_frames;
  guint32 max_qp_p_frames;
  guint32 min_qp_b_frames;
  guint32 max_qp_b_frames;
  guint32 quant_i_frames;
  guint32 quant_p_frames;
  guint32 quant_b_frames;
};

/*
  Class structure should always contain the class structure for the type you're inheriting from.
*/
struct _GstQcodec2VencClass
{
  GstVideoEncoderClass parent_class;

  gboolean (*set_format) (GstQcodec2Venc * encoder,
    GstVideoCodecState * state);

  /* actions */
  GstFlowReturn (*force_idr) (GstQcodec2Venc * encoder);
};

GType gst_qcodec2_venc_get_type (void);

ConfigParams make_profile_level_param (C2W_PROFILE_T profile,
    C2W_LEVEL_T level);

gboolean gst_qcodec2_venc_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* __GST_QCODEC2_VENC_H__ */
