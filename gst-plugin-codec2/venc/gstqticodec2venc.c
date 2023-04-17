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
Changes from Qualcomm Innovation Center are provided under the following license:

Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted (subject to the limitations in the
disclaimer below) provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef GST_PACKAGE_ORIGIN
#   define GST_PACKAGE_ORIGIN "-"
#endif

#include <gst/gst.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "gstqticodec2venc.h"
#include "gstqcodec2h264enc.h"
#include "gstqcodec2h265enc.h"


GST_DEBUG_CATEGORY (gst_qticodec2venc_debug);
#define GST_CAT_DEFAULT gst_qticodec2venc_debug

#define DEFAULT_COLOR_SPACE_CONVERSION            (FALSE)
#define DEFAULT_BITRATE_SAVING_MODE               (0xffffffff)
#define DEFAULT_BLUR_MODE                         (0xffffffff)

/* class initialization */
G_DEFINE_TYPE (Gstqticodec2venc, gst_qticodec2venc, GST_TYPE_VIDEO_ENCODER);

#define GST_TYPE_CODEC2_ENC_MIRROR_TYPE (gst_qticodec2venc_mirror_get_type ())
#define GST_TYPE_CODEC2_ENC_RATE_CONTROL (gst_qticodec2venc_rate_control_get_type ())
#define GST_TYPE_CODEC2_ENC_COLOR_PRIMARIES (gst_qticodec2venc_color_primaries_get_type())
#define GST_TYPE_CODEC2_ENC_MATRIX_COEFFS (gst_qticodec2venc_matrix_coeffs_get_type())
#define GST_TYPE_CODEC2_ENC_TRANSFER_CHAR (gst_qticodec2venc_transfer_characteristics_get_type())
#define GST_TYPE_CODEC2_ENC_FULL_RANGE (gst_qticodec2venc_full_range_get_type())
#define GST_TYPE_CODEC2_ENC_INTRA_REFRESH_MODE (gst_qticodec2venc_intra_refresh_mode_get_type ())
#define GST_TYPE_CODEC2_ENC_SLICE_MODE (gst_qticodec2venc_slice_mode_get_type ())
#define GST_TYPE_CODEC2_ENC_BLUR_MODE (gst_qticodec2venc_blur_mode_get_type ())
#define GST_TYPE_CODEC2_ENC_BITRATE_SAVING_MODE (gst_qticodec2venc_bitrate_saving_mode_get_type ())

#define parent_class gst_qticodec2venc_parent_class
#define NANO_TO_MILLI(x)  ((x) / 1000)
#define EOS_WAITING_TIMEOUT 5
#define MAX_INPUT_BUFFERS 32
#define ROI_ARRAY_SIZE 128

/* Function will be named qticodec2venc_qdata_quark() */
static G_DEFINE_QUARK (QtiCodec2EncoderQuark, qticodec2venc_qdata);

enum
{
  PROP_0,
  PROP_SILENT,
  PROP_RATE_CONTROL,
  PROP_DOWNSCALE_WIDTH,
  PROP_DOWNSCALE_HEIGHT,
  PROP_COLOR_SPACE_PRIMARIES,
  PROP_COLOR_SPACE_MATRIX_COEFFS,
  PROP_COLOR_SPACE_TRANSFER_CHAR,
  PROP_COLOR_SPACE_FULL_RANGE,
  PROP_COLOR_SPACE_CONVERSION,
  PROP_MIRROR,
  PROP_ROTATION,
  PROP_INTRA_REFRESH_MODE,
  PROP_INTRA_REFRESH_MBS,
  PROP_TARGET_BITRATE,
  PROP_SLICE_MODE,
  PROP_SLICE_SIZE,
  PROP_BLUR_MODE,
  PROP_BLUR_WIDTH,
  PROP_BLUR_HEIGHT,
  PROP_ROI,
  PROP_BITRATE_SAVING_MODE,
};

/* GstVideoEncoder base class method */
static gboolean gst_qticodec2venc_stop (GstVideoEncoder * encoder);
static gboolean gst_qticodec2venc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_qticodec2venc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_qticodec2venc_finish (GstVideoEncoder * encoder);
static gboolean gst_qticodec2venc_open (GstVideoEncoder * encoder);
static gboolean gst_qticodec2venc_close (GstVideoEncoder * encoder);
static gboolean gst_qticodec2venc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);

static void gst_qticodec2venc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_qticodec2venc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_qticodec2venc_finalize (GObject * object);

static gboolean gst_qticodec2venc_create_component (GstVideoEncoder * encoder);
static gboolean gst_qticodec2venc_destroy_component (GstVideoEncoder * encoder);
static void handle_video_event (const void *handle, EVENT_TYPE type,
    void *data);

static GstFlowReturn gst_qticodec2venc_encode (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_qticodec2venc_setup_output (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static void gst_qticodec2venc_buffer_release (GstStructure * structure);

static void gst_qticodec2venc_build_roi_array (GstVideoEncoder * encoder,
    const GValue * value);
static gboolean handle_dynamic_meta (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static void build_roi_meta (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static void add_roi_to_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame, GstStructure * roimeta);
static void gst_qticodec2venc_release_frames (GstVideoEncoder * encoder);

static gboolean
gst_qticodec2venc_refresh_input_layout_info (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame, BufferDescriptor * bufinfo);

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

#define GST_QC2VENC_SINK_TEMPLATE_CAP \
    GST_QC2VENC_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_DMABUF,"NV12",128,8192)";" \
    GST_QC2VENC_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_DMABUF,"P010_10LE",128,8192)";" \
    GST_QC2VENC_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_DMABUF,"NV12_10LE32",128,8192)";" \
    GST_QC2VENC_CAPS_MAKE("NV12",128,8192)";" \
    GST_QC2VENC_CAPS_MAKE("P010_10LE",128,8192)";" \
    GST_QC2VENC_CAPS_MAKE("NV12_10LE32",128,8192)

static GstStaticPadTemplate gst_qtivenc_sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_ENCODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_QC2VENC_SINK_TEMPLATE_CAP));

static ConfigParams
make_bitrate_param (guint32 bitrate, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_BITRATE;
  param.isInput = is_input;
  param.val.u32 = bitrate;

  return param;
}

static ConfigParams
make_resolution_param (guint32 width, guint32 height, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_RESOLUTION;
  param.isInput = is_input;
  param.resolution.width = width;
  param.resolution.height = height;

  return param;
}

static ConfigParams
make_pixel_format_param (guint32 fmt, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_PIXELFORMAT;
  param.isInput = is_input;
  param.pixelFormat.fmt = fmt;

  return param;
}

static ConfigParams
make_interlace_param (INTERLACE_MODE_TYPE mode, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_INTERLACE_INFO;
  param.isInput = is_input;
  param.interlaceMode.type = mode;

  return param;
}

static ConfigParams
make_mirror_param (MIRROR_TYPE mirror, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_MIRROR;
  param.isInput = is_input;
  param.mirror.type = mirror;

  return param;
}

static ConfigParams
make_rotation_param (guint32 rotation, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_ROTATION;
  param.isInput = is_input;
  param.val.u32 = rotation;

  return param;
}

static ConfigParams
make_rate_control_param (RC_MODE_TYPE mode)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_RATECONTROL;
  param.rcMode.type = mode;

  return param;
}

static ConfigParams
make_downscale_param (guint32 width, guint32 height)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_DOWNSCALE;
  param.resolution.width = width;
  param.resolution.height = height;

  return param;
}

static ConfigParams
make_slicemode_param (guint32 size, SLICE_MODE mode)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_SLICE_MODE;
  param.sliceMode.slice_size = size;
  param.sliceMode.type = mode;

  return param;
}

static ConfigParams
make_color_space_conv_param (gboolean csc)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_ENC_CSC;
  param.color_space_conversion = csc;

  return param;
}

static ConfigParams
make_color_aspects_param (COLOR_PRIMARIES primaries,
    TRANSFER_CHAR transfer_char, MATRIX matrix, FULL_RANGE full_range)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_COLOR_ASPECTS_INFO;
  param.colorAspects.primaries = primaries;
  param.colorAspects.transfer_char = transfer_char;
  param.colorAspects.matrix = matrix;
  param.colorAspects.full_range = full_range;

  return param;
}

static ConfigParams
make_intra_refresh_param (IR_MODE_TYPE mode, guint32 intra_refresh_mbs)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_INTRAREFRESH;
  param.irMode.type = mode;
  param.irMode.intra_refresh_mbs = intra_refresh_mbs;

  return param;
}

static ConfigParams
make_blur_mode_param (BLUR_MODE mode, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_BLUR_MODE;
  param.isInput = is_input;
  param.blur.mode = mode;

  return param;
}

static ConfigParams
make_blur_resolution_param (guint32 width, guint32 height, gboolean is_input)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_BLUR_RESOLUTION;
  param.isInput = is_input;
  param.resolution.width = width;
  param.resolution.height = height;

  return param;
}

static ConfigParams
make_roi_param (Gstqticodec2venc * enc, const int64_t timestamp,
    const char *type, const char *payload, const char *payloadExt)
{
  ConfigParams param;
  memset (&param, 0, sizeof (ConfigParams));
  memset (enc->roi_type, 0, sizeof (char) * ROI_ARRAY_SIZE);
  memset (enc->roi_rect_payload, 0, sizeof (char) * ROI_ARRAY_SIZE);
  memset (enc->roi_rect_payload_ext, 0, sizeof (char) * ROI_ARRAY_SIZE);

  param.config_name = CONFIG_FUNCTION_KEY_ROIREGION;
  param.roiRegion.timestampUs = timestamp;
  param.roiRegion.type = enc->roi_type;
  param.roiRegion.rectPayload = enc->roi_rect_payload;
  param.roiRegion.rectPayloadExt = enc->roi_rect_payload_ext;

  memcpy (param.roiRegion.type, type, strlen (type));
  memcpy (param.roiRegion.rectPayload, payload, strlen (payload));
  memcpy (param.roiRegion.rectPayloadExt, payloadExt, strlen (payloadExt));

  return param;
}

static ConfigParams
make_bitrate_saving_mode (BITRATE_SAVING_MODE mode, gboolean isInput)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_BITRATE_SAVING_MODE;
  param.isInput = isInput;
  param.bitrate_saving_mode.saving_mode = mode;

  return param;
}

ConfigParams
make_profile_level_param (C2W_PROFILE_T profile, C2W_LEVEL_T level)
{
  ConfigParams param;

  memset (&param, 0, sizeof (ConfigParams));

  param.config_name = CONFIG_FUNCTION_KEY_PROFILE_LEVEL;
  param.profileAndLevel.profile = profile;
  param.profileAndLevel.level = level;

  return param;
}

static gchar *
get_c2_comp_name (GstStructure * structure)
{
  gchar *ret = NULL;

  if (gst_structure_has_name (structure, "video/x-h264")) {
    ret = g_strdup ("c2.qti.avc.encoder");
  } else if (gst_structure_has_name (structure, "video/x-h265")) {
    ret = g_strdup ("c2.qti.hevc.encoder");
  } else if (gst_structure_has_name (structure, "video/x-heic")) {
    ret = g_strdup ("c2.qti.heic.encoder");
  }

  return ret;
}

static guint32
gst_to_c2_pixelformat (GstVideoEncoder * encoder, GstVideoFormat format)
{
  guint32 result = 0;
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);

  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      if (enc->is_ubwc) {
        result = PIXEL_FORMAT_NV12_UBWC;
      } else {
        result = PIXEL_FORMAT_NV12_LINEAR;
      }
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
      result = PIXEL_FORMAT_P010;
      break;
    case GST_VIDEO_FORMAT_NV12_10LE32:
      if (enc->is_ubwc) {
        result = PIXEL_FORMAT_TP10_UBWC;
      } else {
        GST_ERROR_OBJECT (enc, "unsupported format Linear NV12_10LE32 yet");
      }
      break;
    default:
      break;
  }

  GST_DEBUG_OBJECT (enc, "to_c2_pixelformat (%s), c2 format: %d",
      gst_video_format_to_string (format), result);

  return result;
}

static GType
gst_qticodec2venc_mirror_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {MIRROR_NONE, "Mirror None", "none"},
      {MIRROR_VERTICAL, "Mirror Vertical", "vertical"},
      {MIRROR_HORIZONTAL, "Mirror Horizontal", "horizontal"},
      {MIRROR_BOTH, "Mirror Both", "both"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencMirror", values);
  }
  return qtype;
}

static GType
gst_qticodec2venc_slice_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {SLICE_MODE_DISABLE, "Slice Mode Disable", "disable"},
      {SLICE_MODE_MB, "Slice Mode MB", "MB"},
      {SLICE_MODE_BYTES, "Slice Mode Bytes", "bytes"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencSliceMode", values);
  }
  return qtype;
}

static GType
gst_qticodec2venc_blur_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {BLUR_AUTO, "Disable External Blur but Enable Internal Blur. If set "
            "before start, blur is disabled throughout the session.", "auto"},
      {BLUR_MANUAL, "External Dynamic Blur Enable. Must be set before start. "
            "Blur is applied when valid resolution is set.", "manual"},
      {BLUR_DISABLE, "Disable External and Internal Blur.", "disable"},
      {0xffffffff, "Component Default", "default"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencBlurMode", values);
  }
  return qtype;
}

static GType
gst_qticodec2venc_rate_control_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {RC_OFF, "Disable RC", "disable"},
      {RC_CONST, "Constant", "constant"},
      {RC_CBR_VFR, "Constant bitrate, variable framerate", "CBR-VFR"},
      {RC_VBR_CFR, "Variable bitrate, constant framerate", "VBR-CFR"},
      {RC_VBR_VFR, "Variable bitrate, variable framerate", "VBR-VFR"},
      {RC_CQ, "Constant quality", "CQ"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencRateControl", values);
  }
  return qtype;
}

static GType
gst_qticodec2venc_color_primaries_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {COLOR_PRIMARIES_UNSPECIFIED, "primaries are unspecified", "NONE"},
      {COLOR_PRIMARIES_BT709, "Rec.ITU-R BT.709-6 or equivalent", "BT709"},
      {COLOR_PRIMARIES_BT470_M, "Rec.ITU-R BT.470-6 System M or equivalent",
          "BT470_M"},
      {COLOR_PRIMARIES_BT601_625, "Rec.ITU-R BT.601-6 625 or equivalent",
          "BT601_625"},
      {COLOR_PRIMARIES_BT601_525, "Rec.ITU-R BT.601-6 525 or equivalent",
          "BT601_525"},
      {COLOR_PRIMARIES_GENERIC_FILM, "Generic Film", "GENERIC_FILM"},
      {COLOR_PRIMARIES_BT2020, "Rec.ITU-R BT.2020 or equivalent", "BT2020"},
      {COLOR_PRIMARIES_RP431, "SMPTE RP 431-2 or equivalent", "RP431"},
      {COLOR_PRIMARIES_EG432, "SMPTE EG 432-1 or equivalent", "EG432"},
      {COLOR_PRIMARIES_EBU3213, "EBU Tech.3213-E or equivalent", "EBU3213"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencColorPrimaries", values);
  }
  return qtype;
}

static GType
gst_qticodec2venc_matrix_coeffs_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {COLOR_MATRIX_UNSPECIFIED, "Matrix coefficients are unspecified", "NONE"},
      {COLOR_MATRIX_BT709, "Rec.ITU-R BT.709-5 or equivalent", "BT709"},
      {COLOR_MATRIX_FCC47_73_682,
            "FCC Title 47 CFR 73.682 or equivalent (KR=0.30, KB=0.11)",
          "FCC47_73_682"},
      {COLOR_MATRIX_BT601,
            "FCC Title 47 CFR 73.682 or equivalent (KR=0.30, KB=0.11)",
          "BT601"},
      {COLOR_MATRIX_240M, "SMPTE 240M or equivalent", "240M"},
      {COLOR_MATRIX_BT2020, "Rec.ITU-R BT.2020 non-constant luminance",
          "BT2020"},
      {COLOR_MATRIX_BT2020_CONSTANT, "Rec.ITU-R BT.2020 constant luminance",
          "BT2020_CONSTANT"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencMatrixCoeffs", values);
  }
  return qtype;
}

static GType
gst_qticodec2venc_transfer_characteristics_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {COLOR_TRANSFER_UNSPECIFIED, "Transfer is unspecified", "NONE"},
      {COLOR_TRANSFER_LINEAR, "Linear transfer characteristics", "LINEAR"},
      {COLOR_TRANSFER_SRGB, "sRGB or equivalent", "SRGB"},
      {COLOR_TRANSFER_170M, "SMPTE 170M or equivalent (e.g. BT.601/709/2020)",
          "170M"},
      {COLOR_TRANSFER_GAMMA22, "Assumed display gamma 2.2", "GAMMA22"},
      {COLOR_TRANSFER_GAMMA28, "Assumed display gamma 2.8", "GAMMA28"},
      {COLOR_TRANSFER_ST2084, "SMPTE ST 2084 for 10/12/14/16 bit systems",
          "ST2084"},
      {COLOR_TRANSFER_HLG, "ARIB STD-B67 hybrid-log-gamma", "HLG"},
      {COLOR_TRANSFER_240M, "SMPTE 240M or equivalent", "240M"},
      {COLOR_TRANSFER_XVYCC, "IEC 61966-2-4 or equivalent", "XVYCC"},
      {COLOR_TRANSFER_BT1361, "Rec.ITU-R BT.1361 extended gamut", "BT1361"},
      {COLOR_TRANSFER_ST428, "SMPTE ST 428-1 or equivalent", "ST428"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencTransferChar", values);
  }
  return qtype;
}

static GType
gst_qticodec2venc_full_range_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {COLOR_RANGE_UNSPECIFIED, "Range is unspecified", "NONE"},
      {COLOR_RANGE_FULL, "Full range", "FULL"},
      {COLOR_RANGE_LIMITED, "Limited range", "LIMITED"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencFullRange", values);
  }
  return qtype;
}

static GType
gst_qticodec2venc_intra_refresh_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {IR_NONE, "None", "none"},
      {IR_RANDOM, "Random", "random"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencIntraRefreshMode", values);
  }
  return qtype;
}

static GType
gst_qticodec2venc_bitrate_saving_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {BITRATE_SAVING_MODE_DISABLE_ALL, "Bitrate saving mode disable",
          "disable"},
      {BITRATE_SAVING_MODE_ENABLE_8BIT, "8bit bitrate saving Mode enable",
          "8bit"},
      {BITRATE_SAVING_MODE_ENABLE_10BIT, "10bit bitrate saving Mode enable",
          "10bit"},
      {BITRATE_SAVING_MODE_ENABLE_ALL, "All bitrate saving mode enable", "all"},
      {0xffffffff, "Component Default", "default"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstCodec2VencBitrateSavingMode", values);
  }
  return qtype;
}

static gboolean
gst_qticodec2_caps_has_feature (const GstCaps * caps, const gchar * partten)
{
  guint count = gst_caps_get_size (caps);
  gboolean ret = FALSE;

  if (count > 0) {
    for (gint i = 0; i < count; i++) {
      GstCapsFeatures *features = gst_caps_get_features (caps, i);
      if (gst_caps_features_is_any (features))
        continue;
      if (gst_caps_features_contains (features, partten))
        ret = TRUE;
    }
  }

  return ret;
}

static void
parse_roi (GstVideoEncoder * encoder, xmlNodePtr pDynProp)
{
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);

  xmlNodePtr cur = pDynProp->xmlChildrenNode;
  gint id = 0;
  gint64 nFrameNum = -1;

  while (cur) {
    if (!xmlStrcmp (cur->name, (const xmlChar *) "FrameNum")) {
      nFrameNum = strtol ((const char *) cur->children->content, NULL, 10);
    } else if (!xmlStrcmp (cur->name, (const xmlChar *) "ROI")) {
      if (nFrameNum < 0 || (nFrameNum == G_MAXINT64 && errno == ERANGE)) {
        GST_ERROR_OBJECT (enc, "FrameNum out of range or invalid");
        break;
      }

      const char *token = (const char *) cur->children->content;
      static const char *pattern = "%d,%d-%d,%d=%d";
      guint top, left, bottom, right, qp;

      guint count = sscanf (token, pattern,
          &top, &left, &bottom, &right, &qp);
      if (count == 5) {
        GST_DEBUG_OBJECT (enc, "ROI: %ld:%d,%d-%d,%d=%d\n",
            nFrameNum, top, left, bottom, right, qp);

        GstStructure *roimeta = gst_structure_new_empty ("roi-meta");
        if (roimeta) {
          gst_structure_set (roimeta, "frame", G_TYPE_UINT64, nFrameNum, NULL);
          if (bottom == 0 || right == 0) {
            /* region roi info must be configured before encoder start
             * use 0,0-0,0=0 dummy meta to trigger ROI config
             */
            gst_structure_set (roimeta, "roi_type", G_TYPE_STRING, "dummy",
                NULL);
          } else {
            gst_structure_set (roimeta, "roi_type", G_TYPE_STRING, "rect",
                NULL);
          }
          gst_structure_set (roimeta, "id", G_TYPE_INT, id, NULL);
          gst_structure_set (roimeta, "top", G_TYPE_UINT, top, NULL);
          gst_structure_set (roimeta, "left", G_TYPE_UINT, left, NULL);
          gst_structure_set (roimeta, "width", G_TYPE_UINT, right - left, NULL);
          gst_structure_set (roimeta, "height", G_TYPE_UINT, bottom - top,
              NULL);
          gst_structure_set (roimeta, "qp", G_TYPE_UINT, qp, NULL);

          if (enc->roi_array == NULL) {
            enc->roi_array = g_array_new (FALSE, TRUE, sizeof (GstStructure *));
          }

          g_array_append_val (enc->roi_array, roimeta);
          id++;
        }
      } else {
        GST_ERROR_OBJECT (enc, "meta pattern mismatched");
      }
    }
    cur = cur->next;
  }
}

static void
gst_qticodec2venc_build_roi_array (GstVideoEncoder * encoder,
    const GValue * value)
{
  gchar *roi_xml = g_value_dup_string (value);
  if (roi_xml == NULL) {
    return;
  }

  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);
  GST_INFO_OBJECT (enc, "roi config path %s", roi_xml);

  xmlDocPtr doc;
  xmlNodePtr cur;

  doc = xmlParseFile (roi_xml);
  g_free (roi_xml);

  if (doc == NULL) {
    GST_ERROR_OBJECT (enc, "roi document not parsed failed.");
    return;
  }

  cur = xmlDocGetRootElement (doc);

  if (cur == NULL) {
    GST_ERROR_OBJECT (enc, "empty roi document");
    xmlFreeDoc (doc);
    return;
  }
  // find session root
  cur = cur->xmlChildrenNode;
  while (cur) {
    if (!xmlStrcmp (cur->name, (const xmlChar *) "EncodeSession")) {
      cur = cur->xmlChildrenNode;
      break;
    }
    cur = cur->next;
  }

  // find dynamic property node
  while (cur) {
    if (!xmlStrcmp (cur->name, (const xmlChar *) "DynamicProperty")) {
      parse_roi (encoder, cur);
    }

    cur = cur->next;
  }

  xmlFreeDoc (doc);
}

static gboolean
gst_qticodec2venc_create_component (GstVideoEncoder * encoder)
{
  gboolean ret = FALSE;
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);

  GST_DEBUG_OBJECT (enc, "create_component");

  if (enc->comp_store) {

    ret =
        c2componentStore_createComponent (enc->comp_store, enc->comp_name,
        &enc->comp, NULL);
    if (ret == FALSE) {
      GST_DEBUG_OBJECT (enc, "Failed to create component");
    }

    enc->comp_intf = c2component_intf (enc->comp);

    ret =
        c2component_setListener (enc->comp, encoder, handle_video_event,
        BLOCK_MODE_MAY_BLOCK);
    if (ret == FALSE) {
      GST_DEBUG_OBJECT (enc, "Failed to set event handler");
    }

    ret = c2component_createBlockpool (enc->comp, BUFFER_POOL_BASIC_GRAPHIC);
    if (ret == FALSE) {
      GST_DEBUG_OBJECT (enc, "Failed to create graphics pool");
    }
  } else {
    GST_DEBUG_OBJECT (enc, "Component store is Null");
  }

  return ret;
}

static gboolean
gst_qticodec2venc_destroy_component (GstVideoEncoder * encoder)
{
  gboolean ret = FALSE;
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);

  GST_DEBUG_OBJECT (enc, "destroy_component");

  if (enc->comp) {
    c2component_delete (enc->comp);
    enc->comp = NULL;
  }

  return ret;
}

static GstFlowReturn
gst_qticodec2venc_setup_output (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);
  GstFlowReturn ret = GST_FLOW_OK;
  GstCaps *outcaps;

  GST_DEBUG_OBJECT (enc, "setup_output");

  if (enc->output_state) {
    gst_video_codec_state_unref (enc->output_state);
  }

  outcaps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  if (outcaps) {
    GstStructure *structure;
    gchar *comp_name;

    if (gst_caps_is_empty (outcaps)) {
      gst_caps_unref (outcaps);
      GST_ERROR_OBJECT (enc, "Unsupported format in caps: %" GST_PTR_FORMAT,
          outcaps);
      return GST_FLOW_ERROR;
    }

    outcaps = gst_caps_make_writable (outcaps);
    outcaps = gst_caps_fixate (outcaps);
    structure = gst_caps_get_structure (outcaps, 0);

    /* Fill actual width/height into output caps */
    GValue g_width = { 0, };
    GValue g_height = { 0, };
    g_value_init (&g_width, G_TYPE_INT);
    g_value_set_int (&g_width, enc->width);

    g_value_init (&g_height, G_TYPE_INT);
    g_value_set_int (&g_height, enc->height);

    if ((enc->rotation == 90) || (enc->rotation == 270)) {
      gst_caps_set_value (outcaps, "width", &g_height);
      gst_caps_set_value (outcaps, "height", &g_width);
    } else {
      gst_caps_set_value (outcaps, "width", &g_width);
      gst_caps_set_value (outcaps, "height", &g_height);
    }

    GST_INFO_OBJECT (enc, "Fixed output caps: %" GST_PTR_FORMAT, outcaps);

    comp_name = get_c2_comp_name (structure);
    if (!comp_name) {
      GST_ERROR_OBJECT (enc, "Unsupported format in caps: %" GST_PTR_FORMAT,
          outcaps);
      gst_caps_unref (outcaps);
      return GST_FLOW_ERROR;
    }

    enc->comp_name = comp_name;
    enc->output_state =
        gst_video_encoder_set_output_state (encoder, outcaps, state);
    if (!enc->output_state) {
      GST_ERROR_OBJECT (enc, "set output state error");
      gst_caps_unref (outcaps);
      g_free (comp_name);
      return GST_FLOW_ERROR;
    }
    enc->output_setup = TRUE;

    if ((enc->rotation == 90) || (enc->rotation == 270)) {
      enc->output_state->info.width = enc->height;
      enc->output_state->info.height = enc->width;
    }
  }

  return ret;
}

/* Called when the element stops processing. Close external resources. */
static gboolean
gst_qticodec2venc_stop (GstVideoEncoder * encoder)
{
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);

  GST_DEBUG_OBJECT (enc, "stop");
  enc->input_setup = FALSE;
  enc->output_setup = FALSE;

  /* Stop the component */
  if (enc->comp) {
    c2component_stop (enc->comp);
  }

  gst_qticodec2venc_release_frames (encoder);

  return TRUE;
}

/* Dispatch any pending remaining data at EOS. Class can refuse to encode new data after. */
static GstFlowReturn
gst_qticodec2venc_finish (GstVideoEncoder * encoder)
{
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);
  gint64 timeout;
  BufferDescriptor inBuf;

  memset (&inBuf, 0, sizeof (BufferDescriptor));

  GST_DEBUG_OBJECT (enc, "finish");

  inBuf.fd = -1;
  inBuf.data = NULL;
  inBuf.size = 0;
  inBuf.timestamp = 0;
  inBuf.index = enc->frame_index;
  inBuf.flag = FLAG_TYPE_END_OF_STREAM;
  inBuf.pool_type = BUFFER_POOL_BASIC_GRAPHIC;

  /* Setup EOS work */
  if (enc->comp) {
    /* Queue buffer to Codec2 */
    c2component_queue (enc->comp, &inBuf);
  }

  /* wait for all the pending buffers to return */
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  g_mutex_lock (&enc->pending_lock);
  if (!enc->eos_reached) {
    GST_DEBUG_OBJECT (enc, "wait until EOS signal is triggered");

    timeout =
        g_get_monotonic_time () + (EOS_WAITING_TIMEOUT * G_TIME_SPAN_SECOND);
    if (!g_cond_wait_until (&enc->pending_cond, &enc->pending_lock, timeout)) {
      GST_ERROR_OBJECT (enc, "Timed out on wait, exiting!");
    }
  } else {
    GST_DEBUG_OBJECT (enc, "EOS reached on output, finish encoding");
  }

  g_mutex_unlock (&enc->pending_lock);
  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  return GST_FLOW_OK;
}

static gboolean
caps_has_compression (const GstCaps * caps, const gchar * compression)
{
  GstStructure *structure = NULL;
  const gchar *string = NULL;

  structure = gst_caps_get_structure (caps, 0);
  string = gst_structure_has_field (structure, "compression") ?
      gst_structure_get_string (structure, "compression") : NULL;

  return (g_strcmp0 (string, compression) == 0) ? TRUE : FALSE;
}

/* Called to inform the caps describing input video data that encoder is about to receive.
  Might be called more than once, if changing input parameters require reconfiguration. */
static gboolean
gst_qticodec2venc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);
  Gstqticodec2vencClass *enc_class = GST_QTICODEC2VENC_GET_CLASS (encoder);
  GstStructure *structure;
  const gchar *mode;
  const gchar *fmt;
  gint retval = 0;
  gint width = 0;
  gint height = 0;
  GstVideoFormat input_format = GST_VIDEO_FORMAT_UNKNOWN;
  GstVideoInterlaceMode interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
  INTERLACE_MODE_TYPE c2interlace_mode = INTERLACE_MODE_PROGRESSIVE;
  GPtrArray *config = NULL;
  ConfigParams resolution;
  ConfigParams interlace;
  ConfigParams pixelformat;
  ConfigParams mirror;
  ConfigParams rotation;
  ConfigParams rate_control;
  ConfigParams downscale;
  ConfigParams color_space_conversion;
  ConfigParams color_aspects;
  ConfigParams intra_refresh;
  ConfigParams bitrate;
  ConfigParams slice_mode;
  ConfigParams blur_info;
  ConfigParams bitrate_saving_mode;

  GST_DEBUG_OBJECT (enc, "set_format");

  structure = gst_caps_get_structure (state->caps, 0);
  retval = gst_structure_get_int (structure, "width", &width);
  retval &= gst_structure_get_int (structure, "height", &height);
  if (!retval) {
    goto error_res;
  }

  fmt = gst_structure_get_string (structure, "format");
  if (fmt) {
    input_format = gst_video_format_from_string (fmt);
    if (input_format == GST_VIDEO_FORMAT_UNKNOWN) {
      goto error_format;
    }
  }

  GST_DEBUG_OBJECT (enc, "caps: %" GST_PTR_FORMAT, state->caps);
  enc->is_ubwc = caps_has_compression (state->caps, "ubwc");
  GST_DEBUG_OBJECT (enc, "Fixed color format:%s, UBWC:%d", fmt, enc->is_ubwc);

  gst_video_info_from_caps (&enc->input_info, state->caps);

  if (enc->input_setup) {
    /* Already setup, check to see if something has changed on input caps... */
    if ((enc->width == width) && (enc->height == height)) {
      goto done;                /* Nothing has changed */
    } else {
      gst_qticodec2venc_stop (encoder);
    }
  }

  if ((mode = gst_structure_get_string (structure, "interlace-mode"))) {
    if (g_str_equal ("progressive", mode)) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
      c2interlace_mode = INTERLACE_MODE_PROGRESSIVE;
    } else if (g_str_equal ("interleaved", mode)) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
      c2interlace_mode = INTERLACE_MODE_INTERLEAVED_TOP_FIRST;
    } else if (g_str_equal ("mixed", mode)) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
      c2interlace_mode = INTERLACE_MODE_INTERLEAVED_TOP_FIRST;
    } else if (g_str_equal ("fields", mode)) {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_FIELDS;
      c2interlace_mode = INTERLACE_MODE_FIELD_TOP_FIRST;
    }
  }

  enc->width = width;
  enc->height = height;
  enc->interlace_mode = interlace_mode;
  enc->input_format = input_format;

  if (enc->input_state) {
    gst_video_codec_state_unref (enc->input_state);
  }

  enc->input_state = gst_video_codec_state_ref (state);

  if (GST_FLOW_OK != gst_qticodec2venc_setup_output (encoder, state)) {
    GST_ERROR_OBJECT (enc, "fail to setup output");
    goto error_output;
  }

  if (!gst_video_encoder_negotiate (encoder)) {
    GST_ERROR_OBJECT (enc, "Failed to negotiate with downstream");
    goto error_output;
  }

  config = g_ptr_array_new ();

  if (enc->target_bitrate > 0) {
    bitrate = make_bitrate_param (enc->target_bitrate, FALSE);
    g_ptr_array_add (config, &bitrate);
    GST_DEBUG_OBJECT (enc, "set target bitrate:%u", enc->target_bitrate);
  }

  if (enc->bitrate_saving_mode != DEFAULT_BITRATE_SAVING_MODE) {
    bitrate_saving_mode =
        make_bitrate_saving_mode (enc->bitrate_saving_mode, FALSE);
    g_ptr_array_add (config, &bitrate_saving_mode);
  }

  resolution = make_resolution_param (width, height, TRUE);
  g_ptr_array_add (config, &resolution);

  pixelformat =
      make_pixel_format_param (gst_to_c2_pixelformat (encoder, input_format),
      TRUE);
  g_ptr_array_add (config, &pixelformat);

  rate_control = make_rate_control_param (enc->rcMode);
  g_ptr_array_add (config, &rate_control);

  if (enc->mirror != MIRROR_NONE) {
    mirror = make_mirror_param (enc->mirror, TRUE);
    g_ptr_array_add (config, &mirror);
  }

  if (enc->rotation > 0) {
    rotation = make_rotation_param (enc->rotation, TRUE);
    g_ptr_array_add (config, &rotation);
  }

  if (enc->downscale_width > 0 && enc->downscale_height > 0) {
    downscale =
        make_downscale_param (enc->downscale_width, enc->downscale_height);
    g_ptr_array_add (config, &downscale);
  }

  if (enc->slice_mode != SLICE_MODE_DISABLE) {
    slice_mode = make_slicemode_param (enc->slice_size, enc->slice_mode);
    g_ptr_array_add (config, &slice_mode);
  }

  if (enc->color_space_conversion) {
    GST_DEBUG_OBJECT (enc, "enable color space conversion");
    color_space_conversion =
        make_color_space_conv_param (enc->color_space_conversion);
    g_ptr_array_add (config, &color_space_conversion);
    GST_DEBUG_OBJECT (enc, "set color aspect info");
    color_aspects =
        make_color_aspects_param (enc->primaries, enc->transfer_char,
        enc->matrix, enc->full_range);
    g_ptr_array_add (config, &color_aspects);
  }

  if (enc->intra_refresh_mode && enc->intra_refresh_mbs) {
    GST_DEBUG_OBJECT (enc, "set intra refresh mode: %d, mbs:%d",
        enc->intra_refresh_mode, enc->intra_refresh_mbs);
    intra_refresh =
        make_intra_refresh_param (enc->intra_refresh_mode,
        enc->intra_refresh_mbs);
    g_ptr_array_add (config, &intra_refresh);
  }

  if (enc->blur_mode != DEFAULT_BLUR_MODE) {
    if ((enc->blur_mode == BLUR_MANUAL) &&
        (enc->blur_width != 0) && (enc->blur_height != 0)) {
      blur_info =
          make_blur_resolution_param (enc->blur_width, enc->blur_height, TRUE);
    } else {
      blur_info = make_blur_mode_param (enc->blur_mode, TRUE);
    }
    g_ptr_array_add (config, &blur_info);
  }
  /* Create component */
  if (!gst_qticodec2venc_create_component (encoder)) {
    GST_ERROR_OBJECT (enc, "Failed to create component");
  }

  GST_DEBUG_OBJECT (enc,
      "set graphic pool with: %d, height: %d, format: %x, rc mode: %d",
      enc->width, enc->height, enc->input_format, enc->rcMode);

  if (!c2componentInterface_initReflectedParamUpdater (enc->comp_store,
          enc->comp_intf)) {
    GST_WARNING_OBJECT (enc, "Failed to init ReflectedParamUpdater");
  }

  if (!c2componentInterface_config (enc->comp_intf,
          config, BLOCK_MODE_MAY_BLOCK)) {
    GST_WARNING_OBJECT (enc, "Failed to set encoder config");
  }

  g_ptr_array_free (config, TRUE);

  if (enc_class->set_format) {
    if (!enc_class->set_format (enc, state)) {
      GST_ERROR_OBJECT (enc, "Subclass failed to set the new format");
      return FALSE;
    }
  }

  if (!c2component_start (enc->comp)) {
    GST_DEBUG_OBJECT (enc, "Failed to start component");
    goto error_config;
  }

  GST_DEBUG_OBJECT (enc, "c2 component started");

done:
  enc->input_setup = TRUE;
  return TRUE;

  /* Errors */
error_format:
  {
    GST_ERROR_OBJECT (enc, "Unsupported format in caps: %" GST_PTR_FORMAT,
        state->caps);
    return FALSE;
  }
error_res:
  {
    GST_ERROR_OBJECT (enc, "Unable to get width/height value");
    return FALSE;
  }
error_output:
  {
    GST_ERROR_OBJECT (enc, "Unable to set output state");
    return FALSE;
  }
error_config:
  {
    GST_ERROR_OBJECT (enc, "Unable to configure the component");
    return FALSE;
  }

  return TRUE;
}

static void
gst_qticodec2venc_release_frames (GstVideoEncoder * encoder)
{
  GList *frames;

  GST_DEBUG_OBJECT (encoder, "release remain frames");
  frames = gst_video_encoder_get_frames (encoder);
  /* First unref is used for decreasing ref_count since gst_video_encoder_get_frames.
   * Second one is used for decreasing ref_count for remain frames which
   * have not released. Since c2 component has been stoped by accident, e.g.
   * element state change to NULL. It leads to C2 listener closed. Further,
   * function gst_video_encoder_finish_frame in function handle_video_event
   * will not be called. */
  g_list_foreach (frames, (GFunc) gst_video_codec_frame_unref, NULL);
  g_list_free_full (frames, (GDestroyNotify) gst_video_codec_frame_unref);
}

/* Called when the element changes to GST_STATE_READY */
static gboolean
gst_qticodec2venc_open (GstVideoEncoder * encoder)
{
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (enc, "open");

  enc->comp = NULL;
  enc->comp_intf = NULL;
  enc->input_setup = FALSE;
  enc->output_setup = FALSE;
  enc->eos_reached = FALSE;
  enc->input_state = NULL;
  enc->output_state = NULL;
  enc->pool = NULL;
  enc->width = 0;
  enc->height = 0;
  enc->frame_index = 0;
  enc->num_input_queued = 0;
  enc->num_output_done = 0;

  memset (enc->queued_frame, 0, MAX_QUEUED_FRAME);

  /* Create component store */
  enc->comp_store = c2componentStore_create ();

  return ret;
}

/* Called when the element changes to GST_STATE_NULL */
static gboolean
gst_qticodec2venc_close (GstVideoEncoder * encoder)
{
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);

  GST_DEBUG_OBJECT (enc, "qticodec2venc_close");

  gst_qticodec2venc_destroy_component (GST_VIDEO_ENCODER (enc));

  if (enc->comp_store) {
    c2componentStore_delete (enc->comp_store);
    enc->comp_store = NULL;
  }

  if (enc->input_state) {
    gst_video_codec_state_unref (enc->input_state);
    enc->input_state = NULL;
  }

  if (enc->output_state) {
    gst_video_codec_state_unref (enc->output_state);
    enc->output_state = NULL;
  }

  return TRUE;
}

/* Called whenever a input frame from the upstream is sent to encoder */
static GstFlowReturn
gst_qticodec2venc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (enc, "handle_frame");

  if (!enc->input_setup) {
    return GST_FLOW_OK;
  }

  if (!enc->output_setup) {
    return GST_FLOW_ERROR;
  }

  GST_DEBUG ("Frame number : %d, pts: %" GST_TIME_FORMAT,
      frame->system_frame_number, GST_TIME_ARGS (frame->pts));

  /* Encode frame */
  if (frame) {
    return gst_qticodec2venc_encode (encoder, frame);
  } else {
    return GST_FLOW_EOS;
  }
}

static gboolean
gst_qticodec2venc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query)
{
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);
  GstCaps *caps;
  GstVideoInfo info;
  GstAllocator *allocator = NULL;
  guint num_max_buffers = MAX_INPUT_BUFFERS;
  GstBufferPoolInitParam param;
  memset (&param, 0, sizeof (GstBufferPoolInitParam));

  gst_query_parse_allocation (query, &caps, NULL);

  if (!caps) {
    GST_INFO_OBJECT (encoder, "failed to get caps");
    goto cleanup;
  }

  GST_INFO_OBJECT (enc, "allocation caps: %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_INFO_OBJECT (encoder, "failed to get video info");
    goto cleanup;
  }

  /* Propose GBM backed memory if upstream has dmabuf feature */
  if (gst_qticodec2_caps_has_feature (caps, GST_CAPS_FEATURE_MEMORY_DMABUF)) {
    param.is_ubwc = enc->is_ubwc;
    param.c2_comp = enc->comp;
    param.info = info;
    param.mode = DMABUF_MODE;
    enc->pool = gst_qcodec2_buffer_pool_new (&param);

    if (!enc->pool)
      goto cleanup;

    if (enc->pool) {
      GstStructure *config;
      GstAllocationParams params = { 0, 0, 0, 0, };

      config = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (enc->pool));

      if (!gst_buffer_pool_config_get_allocator (config, &allocator, NULL)) {
        gst_structure_free (config);
        GST_ERROR_OBJECT (enc, "failed to get allocator from pool");
        goto cleanup;
      } else {
        gst_query_add_allocation_param (query, allocator, &params);
      }

      gst_structure_free (config);

      /* add pool into allocation query */
      gst_query_add_allocation_pool (query, enc->pool,
          GST_VIDEO_INFO_SIZE (&info), 0, num_max_buffers);
      gst_object_unref (enc->pool);

      /* add c2buf meta into allocation query */
      gst_query_add_allocation_meta (query, GST_VIDEO_C2BUF_META_API_TYPE,
          NULL);
    }
  } else {
    GST_INFO_OBJECT (enc,
        "peer component does not support dmabuf feature: %" GST_PTR_FORMAT,
        caps);
  }

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);

cleanup:
  if (enc->pool)
    gst_object_unref (enc->pool);

  return FALSE;
}

/* Push encoded frame to downstream element */
static GstFlowReturn
push_frame_downstream (GstVideoEncoder * encoder, BufferDescriptor * encode_buf)
{
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoCodecFrame *frame = NULL;
  GstMapInfo map = GST_MAP_INFO_INIT;
  GstBuffer *outbuf = NULL;
  GstVideoCodecState *state = NULL;
  GstVideoInfo *vinfo = NULL;
  GstStructure *structure = NULL;

  GST_DEBUG_OBJECT (enc, "push frame downstream");

  state = gst_video_encoder_get_output_state (encoder);
  if (state) {
    vinfo = &state->info;
  } else {
    GST_ERROR_OBJECT (enc, "video codec state is NULL, unexpected!");
    goto out;
  }

  frame = gst_video_encoder_get_frame (encoder, encode_buf->index);
  if (frame == NULL) {
    GST_ERROR_OBJECT (enc,
        "Error in gst_video_encoder_get_frame, frame number: %lu",
        encode_buf->index);
    goto out;
  }

  if (encode_buf->flag & FLAG_TYPE_CODEC_CONFIG) {
    GST_DEBUG_OBJECT (enc, "fill codec config size:%d first frame size:%d",
        encode_buf->config_size, encode_buf->size);
    outbuf =
        gst_buffer_new_and_alloc (encode_buf->size + encode_buf->config_size);
    gst_buffer_fill (outbuf, 0, encode_buf->config_data,
        encode_buf->config_size);
    gst_buffer_fill (outbuf, encode_buf->config_size, encode_buf->data,
        encode_buf->size);
  } else {
    outbuf = gst_buffer_new_and_alloc (encode_buf->size);
    gst_buffer_fill (outbuf, 0, encode_buf->data, encode_buf->size);
  }

  if (outbuf) {
    GST_BUFFER_TIMESTAMP (outbuf) =
        gst_util_uint64_scale (encode_buf->timestamp, GST_SECOND,
        C2_TICKS_PER_SECOND);

    if (vinfo->fps_n > 0) {
      GST_BUFFER_DURATION (outbuf) = gst_util_uint64_scale (GST_SECOND,
          vinfo->fps_d, vinfo->fps_n);
    }

    GST_DEBUG_OBJECT (enc,
        "out buffer: %p, PTS: %lu, duration: %lu, fps_d: %d, fps_n: %d", outbuf,
        GST_BUFFER_PTS (outbuf), GST_BUFFER_DURATION (outbuf), vinfo->fps_d,
        vinfo->fps_n);

    /* Creates a new, empty GstStructure with the given name */
    structure = gst_structure_new_empty ("BUFFER");
    gst_structure_set (structure,
        "encoder", G_TYPE_POINTER, encoder,
        "index", G_TYPE_UINT64, encode_buf->index, NULL);
    /* Set a notification function to signal when the buffer is no longer used. */
    gst_mini_object_set_qdata (GST_MINI_OBJECT (outbuf),
        qticodec2venc_qdata_quark (), structure,
        (GDestroyNotify) gst_qticodec2venc_buffer_release);

    frame->output_buffer = outbuf;
    gst_video_codec_frame_unref (frame);
    ret = gst_video_encoder_finish_frame (encoder, frame);
    if (ret == GST_FLOW_FLUSHING) {
      GST_WARNING_OBJECT (enc, "downstream is flushing");
    } else if (ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (enc, "Failed to finish frame, outbuf: %p", outbuf);
      goto out;
    }
  } else {
    GST_ERROR_OBJECT (enc, "Failed to create outbuf");
    goto out;
  }

  return ret;

out:
  return GST_FLOW_ERROR;
}

static void
gst_qticodec2venc_buffer_release (GstStructure * structure)
{
  GstVideoEncoder *encoder = NULL;
  Gstqticodec2venc *dec = NULL;
  guint64 index = 0;

  gst_structure_get (structure, "encoder", G_TYPE_POINTER, &encoder, NULL);
  gst_structure_get_uint64 (structure, "index", &index);

  if (encoder) {
    Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);

    GST_LOG_OBJECT (enc, "gst_qticodec2venc_buffer_release");

    if (!c2component_freeOutBuffer (enc->comp, index)) {
      GST_ERROR_OBJECT (dec, "Failed to release the buffer (%lu)", index);
    }
  } else {
    GST_ERROR_OBJECT (dec, "Null handle");
  }

  gst_structure_free (structure);
}

/* Handle event from Codec2 */
static void
handle_video_event (const void *handle, EVENT_TYPE type, void *data)
{
  GstVideoEncoder *encoder = (GstVideoEncoder *) handle;
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (enc, "handle_video_event");

  switch (type) {
    case EVENT_OUTPUTS_DONE:{
      BufferDescriptor *outBuffer = (BufferDescriptor *) data;

      GST_DEBUG_OBJECT (enc,
          "Event output done, va: %p, offsets: %" G_GSIZE_FORMAT " %"
          G_GSIZE_FORMAT ", index: %lu, fd: %u,"
          "filled len: %u, buffer size: %u, timestamp: %lu, flag: %x",
          outBuffer->data, outBuffer->offset[0], outBuffer->offset[1],
          outBuffer->index, outBuffer->fd, outBuffer->size, outBuffer->capacity,
          outBuffer->timestamp, outBuffer->flag);

      if (outBuffer->fd > 0 || outBuffer->size > 0) {
        ret = push_frame_downstream (encoder, outBuffer);
        if (ret != GST_FLOW_FLUSHING && ret != GST_FLOW_OK) {
          GST_ERROR_OBJECT (enc, "Failed to push frame downstream");
        }
      } else if (outBuffer->flag & FLAG_TYPE_END_OF_STREAM) {
        GST_INFO_OBJECT (enc, "Encoder reached EOS");
        g_mutex_lock (&enc->pending_lock);
        enc->eos_reached = TRUE;
        g_cond_signal (&enc->pending_cond);
        g_mutex_unlock (&enc->pending_lock);
      } else {
        GST_ERROR_OBJECT (enc, "Invalid output buffer");
      }
      break;
    }
    case EVENT_TRIPPED:{
      GST_ERROR_OBJECT (enc, "EVENT_TRIPPED(%d)", *(gint32 *) data);
      break;
    }
    case EVENT_ERROR:{
      GST_ERROR_OBJECT (enc, "EVENT_ERROR(%d)", *(gint32 *) data);
      GST_ELEMENT_ERROR (enc, STREAM, ENCODE, ("Encoder posts an error"),
          (NULL));
      break;
    }
    default:{
      GST_ERROR_OBJECT (enc, "Invalid Event(%d)", type);
    }
  }
}

static void
_free_roi_struct (Gstqticodec2venc * enc)
{
  if (enc->roi_type) {
    g_free (enc->roi_type);
    enc->roi_type = NULL;
  }
  if (enc->roi_rect_payload) {
    g_free (enc->roi_rect_payload);
    enc->roi_rect_payload = NULL;
  }
  if (enc->roi_rect_payload_ext) {
    g_free (enc->roi_rect_payload_ext);
    enc->roi_rect_payload_ext = NULL;
  }
}

static gboolean
_allocate_roi_struct (Gstqticodec2venc * enc)
{
  gboolean ret = TRUE;

  /* allocate these structures only once */
  if (!enc->roi_type)
    enc->roi_type = g_malloc (ROI_ARRAY_SIZE * sizeof (char));
  if (!enc->roi_rect_payload)
    enc->roi_rect_payload = g_malloc (ROI_ARRAY_SIZE * sizeof (char));
  if (!enc->roi_rect_payload_ext)
    enc->roi_rect_payload_ext = g_malloc (ROI_ARRAY_SIZE * sizeof (char));

  if (!enc->roi_type || !enc->roi_rect_payload || !enc->roi_rect_payload_ext) {
    _free_roi_struct (enc);
    GST_ERROR_OBJECT (enc, "Failed to allocate ROI structure");
    ret = FALSE;
  }

  return ret;
}

static gboolean
handle_dynamic_meta (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);
  GstMeta *meta = NULL;
  gpointer state = NULL;
  gboolean result = TRUE;
  GPtrArray *config = NULL;

  gchar roi_config_param[ROI_ARRAY_SIZE];
  gchar roi_type[ROI_ARRAY_SIZE];
  gint config_param_len = 0;
  memset (roi_config_param, 0, sizeof (roi_config_param));
  memset (roi_type, 0, sizeof (roi_type));

  while ((meta =
          gst_buffer_iterate_meta_filtered (frame->input_buffer, &state,
              GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE))) {

    GstVideoRegionOfInterestMeta *roi = (GstVideoRegionOfInterestMeta *) meta;
    GstStructure *roimeta =
        gst_video_region_of_interest_meta_get_param (roi, "roi-meta");
    if (roimeta) {
      guint right = roi->x + roi->w;
      guint bottom = roi->y + roi->h;
      guint qp;
      gst_structure_get_uint (roimeta, "qp", &qp);

      char rect_qp[ROI_ARRAY_SIZE];
      gint rect_qp_len =
          g_snprintf (rect_qp, sizeof (rect_qp), "%d,%d-%d,%d=%d;",
          roi->y, roi->x, bottom, right, qp);
      if (config_param_len + rect_qp_len < sizeof (roi_config_param)) {
        config_param_len =
            g_strlcat (roi_config_param, rect_qp, sizeof (roi_config_param));
      } else {
        GST_WARNING_OBJECT (enc, "failed to append roi for frame[%lu:%d]=%s, "
            "will ignore subsequent roi parameters", enc->frame_index, roi->id,
            rect_qp);
        g_warn_if_fail (FALSE && "failed to append roi");
        break;
      }

      g_strlcpy (roi_type, g_quark_to_string (roi->roi_type),
          sizeof (roi_type));
    }
  }

  if (config_param_len > 0) {
    config = g_ptr_array_new ();
    if (config) {
      ConfigParams roiRegion;
      if (_allocate_roi_struct (enc) == FALSE) {
        result = FALSE;
        goto out;
      }
      roiRegion = make_roi_param (enc, NANO_TO_MILLI (frame->pts),
          roi_type, roi_config_param, roi_config_param);

      GST_INFO_OBJECT (enc, "frame[%lu]: roi_type %s, %s",
          enc->frame_index, roi_type, roi_config_param);
      g_ptr_array_add (config, &roiRegion);

      if (!c2componentInterface_config (enc->comp_intf,
              config, BLOCK_MODE_MAY_BLOCK)) {
        GST_WARNING_OBJECT (enc, "Failed to set encoder config for ROI");
      }
    }
  }

out:
  if (config)
    g_ptr_array_free (config, TRUE);
  return result;
}

static void
add_roi_to_frame (GstVideoEncoder * encoder, GstVideoCodecFrame * frame,
    GstStructure * roimeta)
{
  if (encoder == NULL || frame == NULL || roimeta == NULL) {
    return;
  }

  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);

  guint x, y, w, h, qp;
  gst_structure_get_uint (roimeta, "left", &x);
  gst_structure_get_uint (roimeta, "top", &y);
  gst_structure_get_uint (roimeta, "width", &w);
  gst_structure_get_uint (roimeta, "height", &h);
  gst_structure_get_uint (roimeta, "qp", &qp);

  gint id;
  gst_structure_get_int (roimeta, "id", &id);

  GstVideoRegionOfInterestMeta *meta =
      gst_buffer_add_video_region_of_interest_meta (frame->input_buffer,
      gst_structure_get_string (roimeta, "roi_type"), x, y, w, h);
  if (meta) {
    meta->id = id;

    gst_video_region_of_interest_meta_add_param (meta,
        gst_structure_copy (roimeta));

    GST_DEBUG_OBJECT (enc,
        "frame[%lu] add VideoRegionOfInterestMeta[%d] %d-%d-%d-%d=%d",
        enc->frame_index, id, y, x, x + w, y + h, qp);
  }
}

static void
build_roi_meta (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  if (encoder == NULL || frame == NULL) {
    return;
  }

  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);

  GArray *array = enc->roi_array;
  if (array) {
    guint64 index = enc->frame_index;
    for (guint i = 0; i < array->len; i++) {
      GstStructure *roimeta = g_array_index (array, GstStructure *, i);
      if (roimeta) {
        guint64 frame_num;
        gst_structure_get_uint64 (roimeta, "frame", &frame_num);
        if (index == frame_num) {
          add_roi_to_frame (encoder, frame, roimeta);
        }
      }
    }
  }
}

static gboolean
gst_qticodec2venc_refresh_input_layout_info (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame, BufferDescriptor * bufinfo)
{
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);

  bufinfo->stride[0] = GST_VIDEO_INFO_COMP_STRIDE (&enc->input_info, 0);
  bufinfo->stride[1] = GST_VIDEO_INFO_COMP_STRIDE (&enc->input_info, 1);
  bufinfo->offset[0] = GST_VIDEO_INFO_COMP_OFFSET (&enc->input_info, 0);
  bufinfo->offset[1] = GST_VIDEO_INFO_COMP_OFFSET (&enc->input_info, 1);

  GST_DEBUG_OBJECT (enc, "layout info width %u, height %u, "
      "stride0 %d, stride1 %d, "
      "offset0 %" G_GSIZE_FORMAT ", offset1 %" G_GSIZE_FORMAT,
      bufinfo->width, bufinfo->height, bufinfo->stride[0],
      bufinfo->stride[1], bufinfo->offset[0], bufinfo->offset[1]);

  const GstVideoMeta *meta = gst_buffer_get_video_meta (frame->input_buffer);
  if (meta) {
    g_return_val_if_fail (meta->format == bufinfo->format, FALSE);
    g_return_val_if_fail (meta->n_planes == 2, FALSE);
    g_return_val_if_fail (meta->stride[0] > 0, FALSE);
    g_return_val_if_fail (meta->stride[0] == meta->stride[1], FALSE);

    GST_INFO_OBJECT (enc, "GstVideoMeta format %d, width %u, height %u, "
        "stride0 %d, stride1 %d, "
        "offset0 %" G_GSIZE_FORMAT ", offset1 %" G_GSIZE_FORMAT,
        meta->format, meta->width, meta->height, meta->stride[0],
        meta->stride[1], meta->offset[0], meta->offset[1]);

    bufinfo->width = meta->width;
    bufinfo->height = meta->height;
    bufinfo->stride[0] = meta->stride[0];
    bufinfo->stride[1] = meta->stride[1];
    bufinfo->offset[0] = meta->offset[0];
    bufinfo->offset[1] = meta->offset[1];
  }

  return TRUE;
}

/* Push frame to Codec2 */
static GstFlowReturn
gst_qticodec2venc_encode (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (encoder);
  GstVideoFrame video_frame;
  BufferDescriptor inBuf;
  GstBuffer *buf = NULL;
  GstMemory *mem;
  GstMapInfo mapinfo = { 0, };
  gboolean mem_mapped = FALSE;
  gboolean status = FALSE;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoC2BufMeta *video_c2buf_meta = NULL;

  GST_DEBUG_OBJECT (enc, "encode");
  if (!frame) {
    GST_WARNING_OBJECT (enc, "frame is NULL, ret GST_FLOW_EOS");
    return GST_FLOW_EOS;
  }

  memset (&inBuf, 0, sizeof (BufferDescriptor));

  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  buf = frame->input_buffer;
  mem = gst_buffer_get_memory (buf, 0);

  if (gst_is_dmabuf_memory (mem)) {
    inBuf.fd = gst_dmabuf_memory_get_fd (mem);
    inBuf.size = gst_memory_get_sizes (mem, NULL, NULL);
    inBuf.data = NULL;
    video_c2buf_meta = gst_buffer_get_video_c2buf_meta (buf);
    if (video_c2buf_meta) {
      inBuf.c2Buffer = video_c2buf_meta->c2_buf;
    }
    GST_DEBUG_OBJECT (enc, "input c2 buffer:%p fd:%d", inBuf.c2Buffer,
        inBuf.fd);
  } else {
    gst_buffer_map (buf, &mapinfo, GST_MAP_READ);
    mem_mapped = TRUE;
    inBuf.fd = -1;
    inBuf.data = mapinfo.data;
    inBuf.size = mapinfo.size;
  }

  inBuf.timestamp = NANO_TO_MILLI (frame->pts);
  inBuf.index = frame->system_frame_number;
  inBuf.pool_type = BUFFER_POOL_BASIC_GRAPHIC;
  inBuf.width = enc->width;
  inBuf.height = enc->height;
  inBuf.format = enc->input_format;
  inBuf.ubwc_flag = enc->is_ubwc;

  gst_memory_unref (mem);

  g_warn_if_fail (gst_qticodec2venc_refresh_input_layout_info (encoder, frame,
          &inBuf) && "invalid input layout info");

  GST_DEBUG_OBJECT (enc,
      "input buffer: fd: %d, va:%p, size: %d, timestamp: %lu, index: %ld, "
      "stride %u, width %u, height %u",
      inBuf.fd, inBuf.data, inBuf.size, inBuf.timestamp, inBuf.index,
      inBuf.stride[0], inBuf.width, inBuf.height);

  build_roi_meta (encoder, frame);

  if (handle_dynamic_meta (encoder, frame) == FALSE) {
    ret = GST_FLOW_ERROR;
    goto out;
  }

  /* Keep track of queued frame */
  enc->queued_frame[(enc->frame_index) % MAX_QUEUED_FRAME] =
      frame->system_frame_number;

  /* Queue buffer to Codec2 */
  status = c2component_queue (enc->comp, &inBuf);

  if (!status) {
    GST_ERROR_OBJECT (enc, "failed to queue input frame to Codec2");
    ret = GST_FLOW_ERROR;
    goto out;
  }

  g_mutex_lock (&(enc->pending_lock));
  enc->frame_index += 1;
  enc->num_input_queued++;
  g_mutex_unlock (&(enc->pending_lock));

out:
  /* unmap the gstbuffer if it's mapped */
  if (mem_mapped) {
    gst_buffer_unmap (buf, &mapinfo);
  }
  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  return ret;
}

static void
gst_qticodec2venc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{

  Gstqticodec2venc *enc = GST_QTICODEC2VENC (object);

  GST_DEBUG_OBJECT (enc, "qticodec2venc_set_property");

  switch (prop_id) {
    case PROP_SILENT:
      enc->silent = g_value_get_boolean (value);
      break;
    case PROP_MIRROR:
      enc->mirror = g_value_get_enum (value);
      break;
    case PROP_ROTATION:
      enc->rotation = g_value_get_uint (value);
      break;
    case PROP_BLUR_MODE:
      enc->blur_mode = g_value_get_enum (value);
      break;
    case PROP_BLUR_WIDTH:
      enc->blur_width = g_value_get_uint (value);
      break;
    case PROP_BLUR_HEIGHT:
      enc->blur_height = g_value_get_uint (value);
      break;
    case PROP_RATE_CONTROL:
      enc->rcMode = g_value_get_enum (value);
      break;
    case PROP_DOWNSCALE_WIDTH:
      enc->downscale_width = g_value_get_uint (value);
      break;
    case PROP_DOWNSCALE_HEIGHT:
      enc->downscale_height = g_value_get_uint (value);
      break;
    case PROP_COLOR_SPACE_PRIMARIES:
      enc->primaries = g_value_get_enum (value);
      break;
    case PROP_COLOR_SPACE_MATRIX_COEFFS:
      enc->matrix = g_value_get_enum (value);
      break;
    case PROP_COLOR_SPACE_TRANSFER_CHAR:
      enc->transfer_char = g_value_get_enum (value);
      break;
    case PROP_COLOR_SPACE_FULL_RANGE:
      enc->full_range = g_value_get_enum (value);
      break;
    case PROP_COLOR_SPACE_CONVERSION:
      enc->color_space_conversion = g_value_get_boolean (value);
      break;
    case PROP_INTRA_REFRESH_MODE:
      enc->intra_refresh_mode = g_value_get_enum (value);
      break;
    case PROP_INTRA_REFRESH_MBS:
      enc->intra_refresh_mbs = g_value_get_uint (value);
      break;
    case PROP_TARGET_BITRATE:
      enc->target_bitrate = g_value_get_uint (value);
      break;
    case PROP_SLICE_SIZE:
      enc->slice_size = g_value_get_uint (value);
      break;
    case PROP_SLICE_MODE:
      enc->slice_mode = g_value_get_enum (value);
      break;
    case PROP_ROI:
      if (enc->roi_array) {
        for (guint i = 0; i < enc->roi_array->len; i++) {
          GstStructure *roimeta =
              g_array_index (enc->roi_array, GstStructure *, i);
          if (roimeta) {
            gst_clear_structure (&roimeta);
          }
        }

        g_array_free (enc->roi_array, TRUE);
        enc->roi_array = NULL;
      }

      gst_qticodec2venc_build_roi_array ((GstVideoEncoder *) object, value);
      break;
    case PROP_BITRATE_SAVING_MODE:
      enc->bitrate_saving_mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qticodec2venc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{

  Gstqticodec2venc *enc = GST_QTICODEC2VENC (object);

  GST_DEBUG_OBJECT (enc, "qticodec2venc_get_property");

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, enc->silent);
      break;
    case PROP_MIRROR:
      g_value_set_enum (value, enc->mirror);
      break;
    case PROP_ROTATION:
      g_value_set_uint (value, enc->rotation);
      break;
    case PROP_BLUR_MODE:
      g_value_set_enum (value, enc->blur_mode);
      break;
    case PROP_BLUR_WIDTH:
      g_value_set_uint (value, enc->blur_width);
      break;
    case PROP_BLUR_HEIGHT:
      g_value_set_uint (value, enc->blur_height);
      break;
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, enc->rcMode);
      break;
    case PROP_DOWNSCALE_WIDTH:
      g_value_set_uint (value, enc->downscale_width);
      break;
    case PROP_DOWNSCALE_HEIGHT:
      g_value_set_uint (value, enc->downscale_height);
      break;
    case PROP_COLOR_SPACE_PRIMARIES:
      g_value_set_enum (value, enc->primaries);
      break;
    case PROP_COLOR_SPACE_MATRIX_COEFFS:
      g_value_set_enum (value, enc->matrix);
      break;
    case PROP_COLOR_SPACE_TRANSFER_CHAR:
      g_value_set_enum (value, enc->transfer_char);
      break;
    case PROP_COLOR_SPACE_FULL_RANGE:
      g_value_set_enum (value, enc->full_range);
      break;
    case PROP_COLOR_SPACE_CONVERSION:
      g_value_set_boolean (value, enc->color_space_conversion);
      break;
    case PROP_INTRA_REFRESH_MODE:
      g_value_set_enum (value, enc->intra_refresh_mode);
      break;
    case PROP_INTRA_REFRESH_MBS:
      g_value_set_uint (value, enc->intra_refresh_mbs);
      break;
    case PROP_TARGET_BITRATE:
      g_value_set_uint (value, enc->target_bitrate);
      break;
    case PROP_SLICE_SIZE:
      g_value_set_uint (value, enc->slice_size);
      break;
    case PROP_SLICE_MODE:
      g_value_set_enum (value, enc->slice_mode);
      break;
    case PROP_BITRATE_SAVING_MODE:
      g_value_set_enum (value, enc->bitrate_saving_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Called during object destruction process */
static void
gst_qticodec2venc_finalize (GObject * object)
{
  Gstqticodec2venc *enc = GST_QTICODEC2VENC (object);

  GST_DEBUG_OBJECT (enc, "finalize");

  g_mutex_clear (&enc->pending_lock);
  g_cond_clear (&enc->pending_cond);

  g_free (enc->comp_name);

  if (enc->comp_name) {
    enc->comp_name = NULL;
  }

  if (enc->roi_array) {
    for (guint i = 0; i < enc->roi_array->len; i++) {
      GstStructure *roimeta = g_array_index (enc->roi_array, GstStructure *, i);
      if (roimeta) {
        gst_clear_structure (&roimeta);
      }
    }

    g_array_free (enc->roi_array, TRUE);
    enc->roi_array = NULL;
  }

  _free_roi_struct (enc);

  /* Lastly chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
plugin_init (GstPlugin * qticodec2venc)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_qticodec2venc_debug, "qticodec2venc",
      0, "QTI GST codec2.0 video encoder");

  if (!gst_element_register (qticodec2venc, "qcodec2h264enc",
          GST_RANK_PRIMARY + 1, GST_TYPE_QCODEC2_H264_ENC)) {
    GST_ERROR ("failed to register element qcodec2h264enc");
    return FALSE;
  }
  if (!gst_element_register (qticodec2venc, "qcodec2h265enc",
          GST_RANK_PRIMARY + 1, GST_TYPE_QCODEC2_H265_ENC)) {
    GST_ERROR ("failed to register element qcodec2h265enc");
    return FALSE;
  }

  return TRUE;
}

/* Initialize the qticodec2venc's class */
static void
gst_qticodec2venc_class_init (Gstqticodec2vencClass * klass)
{
  GstVideoEncoderClass *video_encoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_qtivenc_sink_template));

  /* Set GObject class property */
  gobject_class->set_property = gst_qticodec2venc_set_property;
  gobject_class->get_property = gst_qticodec2venc_get_property;
  gobject_class->finalize = gst_qticodec2venc_finalize;

  /* Add property to this class */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control", "Rate Control",
          "Bitrate control method",
          GST_TYPE_CODEC2_ENC_RATE_CONTROL,
          RC_OFF,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_MIRROR,
      g_param_spec_enum ("mirror", "Mirror Type",
          "Specify the mirror type",
          GST_TYPE_CODEC2_ENC_MIRROR_TYPE,
          MIRROR_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_ROTATION,
      g_param_spec_uint ("rotation", "Rotation",
          "Specify the angle of clockwise rotation. [0|90|180|270]",
          0, 270, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_BLUR_MODE,
      g_param_spec_enum ("blur-mode", "Blur Mode",
          "Specify the blur mode",
          GST_TYPE_CODEC2_ENC_BLUR_MODE,
          DEFAULT_BLUR_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_BLUR_WIDTH,
      g_param_spec_uint ("blur-width", "Blur Width",
          "Specify the blur filter width.",
          0, UINT_MAX, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_BLUR_HEIGHT,
      g_param_spec_uint ("blur-height", "Blur Height",
          "Specify the blur filter height.",
          0, UINT_MAX, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_DOWNSCALE_WIDTH,
      g_param_spec_uint ("downscale-width", "Downscale width",
          "Specify the downscale width", 0, UINT_MAX, 0, G_PARAM_READWRITE));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_DOWNSCALE_HEIGHT, g_param_spec_uint ("downscale-height",
          "Downscale height", "Specify the downscale height", 0, UINT_MAX, 0,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_COLOR_SPACE_PRIMARIES,
      g_param_spec_enum ("color-primaries", "Input colour primaries",
          "Chromaticity coordinates of the source primaries",
          GST_TYPE_CODEC2_ENC_COLOR_PRIMARIES,
          COLOR_PRIMARIES_UNSPECIFIED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class,
      PROP_COLOR_SPACE_MATRIX_COEFFS, g_param_spec_enum ("matrix-coeffs",
          "Input matrix coefficients",
          "Matrix coefficients used in deriving luma and chroma signals from RGB primaries",
          GST_TYPE_CODEC2_ENC_MATRIX_COEFFS, COLOR_MATRIX_UNSPECIFIED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class,
      PROP_COLOR_SPACE_TRANSFER_CHAR, g_param_spec_enum ("transfer-char",
          "Input transfer characteristics",
          "The opto-electronic transfer characteristics to use.",
          GST_TYPE_CODEC2_ENC_TRANSFER_CHAR, COLOR_TRANSFER_UNSPECIFIED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_COLOR_SPACE_FULL_RANGE,
      g_param_spec_enum ("full-range", "Full range flag",
          "Black level and range of the luma and chroma signals.",
          GST_TYPE_CODEC2_ENC_FULL_RANGE,
          COLOR_RANGE_UNSPECIFIED,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_COLOR_SPACE_CONVERSION,
      g_param_spec_boolean ("color-space-conversion", "Color space conversion",
          "If enabled, should be in color space conversion mode",
          DEFAULT_COLOR_SPACE_CONVERSION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_INTRA_REFRESH_MODE,
      g_param_spec_enum ("intra-refresh-mode", "Intra refresh mode",
          "Intra refresh mode, only support random mode. Allow IR only for CBR(_CFR/VFR) RC modes",
          GST_TYPE_CODEC2_ENC_INTRA_REFRESH_MODE,
          IR_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_INTRA_REFRESH_MBS,
      g_param_spec_uint ("intra-refresh-mbs", "Intra refresh mbs/period",
          "For random modes, it means period of intra refresh. Only support random mode.",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TARGET_BITRATE,
      g_param_spec_uint ("target-bitrate", "Target bitrate",
          "Target bitrate in bits per second (0 means not explicitly set bitrate)",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_SLICE_MODE,
      g_param_spec_enum ("slice-mode", "slice mode",
          "Slice mode, support MB and BYTES mode",
          GST_TYPE_CODEC2_ENC_SLICE_MODE,
          SLICE_MODE_DISABLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SLICE_SIZE,
      g_param_spec_uint ("slice-size", "Slice size",
          "Slice size, just set when slice mode setting to MB or Bytes",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ROI,
      g_param_spec_string ("roi", "ROI config",
          "roi xml config file path", NULL,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_BITRATE_SAVING_MODE,
      g_param_spec_enum ("bps-saving-mode", "Bps saving mode",
          "Bitrate saving mode (0xffffffff=component default)",
          GST_TYPE_CODEC2_ENC_BITRATE_SAVING_MODE,
          DEFAULT_BITRATE_SAVING_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  video_encoder_class->stop = GST_DEBUG_FUNCPTR (gst_qticodec2venc_stop);
  video_encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_qticodec2venc_set_format);
  video_encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_qticodec2venc_handle_frame);
  video_encoder_class->finish = GST_DEBUG_FUNCPTR (gst_qticodec2venc_finish);
  video_encoder_class->open = GST_DEBUG_FUNCPTR (gst_qticodec2venc_open);
  video_encoder_class->close = GST_DEBUG_FUNCPTR (gst_qticodec2venc_close);
  video_encoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_qticodec2venc_propose_allocation);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Codec2 video encoder", "Encoder/Video",
      "Video Encoder based on Codec2.0", "QTI");
}

/* Invoked during object instantiation (equivalent C++ constructor).
 * Initialize only those variables that do not change during state change.
 * For other variables, place initialization into function open.*/
static void
gst_qticodec2venc_init (Gstqticodec2venc * enc)
{
  enc->rcMode = RC_OFF;
  enc->mirror = MIRROR_NONE;
  enc->rotation = 0;
  enc->downscale_width = 0;
  enc->downscale_height = 0;
  enc->target_bitrate = 0;
  enc->blur_mode = DEFAULT_BLUR_MODE;
  enc->blur_width = 0;
  enc->blur_height = 0;
  enc->is_ubwc = FALSE;
  enc->roi_array = NULL;
  enc->roi_type = NULL;
  enc->roi_rect_payload = NULL;
  enc->roi_rect_payload_ext = NULL;
  enc->bitrate_saving_mode = DEFAULT_BITRATE_SAVING_MODE;
  enc->silent = FALSE;

  g_cond_init (&enc->pending_cond);
  g_mutex_init (&enc->pending_lock);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qticodec2venc,
    "QTI GST Codec2.0 Video Encoder",
    plugin_init, VERSION, GST_LICENSE_UNKNOWN, PACKAGE_NAME, GST_PACKAGE_ORIGIN)
