/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "videomark.h"

#include <gst/video/video.h>
#include <opencv2/opencv.hpp>

GST_DEBUG_CATEGORY_STATIC (gst_video_mark_debug);
#define GST_CAT_DEFAULT gst_video_mark_debug

/* GstVideoMark properties */
enum
{
  PROP_0,
  PROP_COLOR,
  PROP_LABEL,
  PROP_FONTSCALE
      /* FILL ME */
};

#define DEFAULT_PROP_COLOR  GST_VIDEO_MARK_COLOR_RED
#define DEFAULT_PROP_LABEL  NULL
#define DEFAULT_PROP_FONTSCALE 1.0F

static GstStaticPadTemplate gst_video_mark_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{I420, NV12, NV21, RGB, BGR, RGBA, BGRA, RGBx, BGRx }"))
    );

static GstStaticPadTemplate gst_video_mark_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ I420, NV12, NV21, RGB, BGR, RGBA, BGRA, RGBx, BGRx }"))
    );

static void gst_video_mark_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_mark_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_video_mark_set_info (GstVideoFilter * vfilter, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info);
static GstFlowReturn gst_video_mark_transform_frame_ip (GstVideoFilter * vfilter,
    GstVideoFrame * frame);
static void gst_video_mark_before_transform (GstBaseTransform * transform,
    GstBuffer * buf);

#define GST_TYPE_VIDEO_MARK_COLOR (gst_video_mark_color_get_type ())
static GType
gst_video_mark_color_get_type (void)
{
  static GType video_mark_color_type = 0;
  static const GEnumValue color_types[] = {
    {GST_VIDEO_MARK_COLOR_RED, "Red", "red"},
    {GST_VIDEO_MARK_COLOR_GREEN, "Green", "green"},
    {GST_VIDEO_MARK_COLOR_BLUE, "Blue", "blue"},
    {GST_VIDEO_MARK_COLOR_BLACK, "100% Black", "black"},
    {GST_VIDEO_MARK_COLOR_WHITE, "100% White", "white"},
    {0, NULL, NULL}
  };

  if (!video_mark_color_type) {
    video_mark_color_type =
        g_enum_register_static ("GstVideoMarkColor", color_types);
  }

  return video_mark_color_type;
}

#define gst_video_mark_parent_class parent_class
G_DEFINE_TYPE (GstVideoMark, gst_video_mark, GST_TYPE_VIDEO_FILTER);

static void
gst_video_mark_finalize (GObject * object)
{
  GstVideoMark *video_mark = GST_VIDEO_MARK (object);

  if (video_mark->label) {
    g_free(video_mark->label);
  }

  G_OBJECT_CLASS(parent_class)->finalize (G_OBJECT (video_mark));
}

static void
gst_video_mark_class_init (GstVideoMarkClass * g_class)
{
  GObjectClass *gobject_class = (GObjectClass *) g_class;
  GstElementClass *gstelement_class = (GstElementClass *) g_class;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) g_class;
  GstVideoFilterClass *vfilter_class = (GstVideoFilterClass *) g_class;

  gobject_class->set_property = gst_video_mark_set_property;
  gobject_class->get_property = gst_video_mark_get_property;
  gobject_class->finalize     = gst_video_mark_finalize;

  g_object_class_install_property (gobject_class, PROP_COLOR,
      g_param_spec_enum ("color", "color", "Color format for marking box or label on video",
          GST_TYPE_VIDEO_MARK_COLOR, DEFAULT_PROP_COLOR,
          (GParamFlags) (G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE)));

  g_object_class_install_property (gobject_class, PROP_LABEL,
     g_param_spec_string("label", "Label", "Label name for bounding box",
          DEFAULT_PROP_LABEL,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_FONTSCALE,
     g_param_spec_double ("fontscale", "FontScale",
          "Font Scale", 0.1F, 10.0F, DEFAULT_PROP_FONTSCALE,
          (GParamFlags) (G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (gstelement_class,
      "Video Mark", "Filter/Effect/Video",
      "Drawing label/bounding box on video", "QTI");

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_video_mark_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_video_mark_src_template);

  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_video_mark_before_transform);
  trans_class->transform_ip_on_passthrough = FALSE;

  vfilter_class->set_info = GST_DEBUG_FUNCPTR (gst_video_mark_set_info);
  vfilter_class->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_video_mark_transform_frame_ip);
}

static void
gst_video_mark_init (GstVideoMark * video_mark)
{
  /* properties */
  video_mark->color = DEFAULT_PROP_COLOR;
  video_mark->label = DEFAULT_PROP_LABEL;
}

static void
gst_video_mark_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstVideoMark *video_mark = GST_VIDEO_MARK (object);

  switch (prop_id) {
    case PROP_COLOR:{
      GstVideoMarkColor val = (GstVideoMarkColor) g_value_get_enum (value);
      GST_DEBUG_OBJECT (video_mark, "Changing color from %u to %u", video_mark->color,
          val);
      GST_OBJECT_LOCK (video_mark);
      video_mark->color = val;
      GST_OBJECT_UNLOCK (video_mark);
      break;
    }
    case PROP_LABEL: {
      GST_OBJECT_LOCK (video_mark);
      g_free (video_mark->label);
      video_mark->label = g_strdup (g_value_get_string (value));
      GST_OBJECT_UNLOCK (video_mark);
      break;
    }
    case PROP_FONTSCALE:
      GST_OBJECT_LOCK (video_mark);
      video_mark->fontscale = g_value_get_double (value);
      GST_OBJECT_UNLOCK (video_mark);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_mark_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoMark *video_mark = GST_VIDEO_MARK (object);

  switch (prop_id) {
    case PROP_COLOR:
      g_value_set_enum (value, video_mark->color);
      break;
    case PROP_LABEL:
      g_value_set_string (value, video_mark->label);
      break;
    case PROP_FONTSCALE:
      g_value_set_double (value, video_mark->fontscale);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static const std::vector<cv::Scalar> i420_scalar_table = {
  cv::Scalar(65, 100, 212),
  cv::Scalar(35, 212, 114),
  cv::Scalar(112, 72, 58),
  cv::Scalar(16, 128, 128),
  cv::Scalar(180, 128, 128),
};

static const std::vector<cv::Scalar> nv12_scalar_table = {
  cv::Scalar(65, 100, 212),
  cv::Scalar(35, 212, 114),
  cv::Scalar(112, 72, 58),
  cv::Scalar(16, 128, 128),
  cv::Scalar(180, 128, 128),
};

static const std::vector<cv::Scalar> nv21_scalar_table = {
  cv::Scalar(65, 212, 100),
  cv::Scalar(35, 114, 212),
  cv::Scalar(112, 58, 72),
  cv::Scalar(16, 128, 128),
  cv::Scalar(180, 128, 128),
};

static const std::vector<cv::Scalar> rgb_scalar_table = {
  cv::Scalar(255, 0, 0),
  cv::Scalar(0, 255, 0),
  cv::Scalar(0, 0, 255),
  cv::Scalar(0, 0, 0),
  cv::Scalar(255, 255, 255),
};

static const std::vector<cv::Scalar> bgr_scalar_table = {
  cv::Scalar(0, 0, 255),
  cv::Scalar(0, 255, 0),
  cv::Scalar(255, 0, 0),
  cv::Scalar(0, 0, 0),
  cv::Scalar(255, 255, 255),
};

static const std::vector<cv::Scalar> rgbx_scalar_table = {
  cv::Scalar(255, 0, 0, 255),
  cv::Scalar(0, 255, 0, 255),
  cv::Scalar(0, 0, 255, 255),
  cv::Scalar(0, 0, 0, 255),
  cv::Scalar(255, 255, 255, 255),
};

static const std::vector<cv::Scalar> bgrx_scalar_table = {
  cv::Scalar(0, 0, 255, 255),
  cv::Scalar(0, 255, 0, 255),
  cv::Scalar(255, 0, 0, 255),
  cv::Scalar(0, 0, 0, 255),
  cv::Scalar(255, 255, 255, 255),
};

static const cv::Scalar &
gst_video_mark_find_color(GstVideoFormat gst_video_fmt, GstVideoMarkColor color)
{
  int color_idx = (int) color;

  if (color < GST_VIDEO_MARK_COLOR_RED || color > GST_VIDEO_MARK_COLOR_WHITE) {
    color_idx = 0;
  }

  switch (gst_video_fmt) {
    case GST_VIDEO_FORMAT_I420:
      return i420_scalar_table[color_idx];
      break;
    case GST_VIDEO_FORMAT_NV12:
      return nv12_scalar_table[color_idx];
      break;
    case GST_VIDEO_FORMAT_NV21:
      return nv12_scalar_table[color_idx];
      break;
    case GST_VIDEO_FORMAT_BGR:
      return bgr_scalar_table[color_idx];
      break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
      return rgbx_scalar_table[color_idx];
      break;
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_BGRx:
      return bgrx_scalar_table[color_idx];
      break;
    case GST_VIDEO_FORMAT_RGB:
    default:
      return rgb_scalar_table[color_idx];
      break;
  }
}

static int
gst_video_mark_gstfmt_to_cvtype(GstVideoFormat gst_video_fmt)
{
  int cv_fmt = CV_8UC3;

  switch (gst_video_fmt) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      cv_fmt = CV_8UC1;
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      cv_fmt = CV_8UC3;
      break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      cv_fmt = CV_8UC4;
      break;
    default:
      cv_fmt = CV_8UC3;
      break;
  }

  return cv_fmt;
}

static void
gst_video_mark_ip (GstVideoMark * video_mark, GstVideoFrame * frame)
{
  GstMeta *meta = NULL;
  gpointer state = NULL;
  int cv_type;
  GstVideoRegionOfInterestMeta * roi_meta;
  GstVideoFormat gst_video_fmt = GST_VIDEO_FRAME_FORMAT (frame);
  gint width = GST_VIDEO_FRAME_WIDTH (frame);
  gint height = GST_VIDEO_FRAME_HEIGHT (frame);
  uint8_t *data = static_cast<uint8_t *>GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  GstStructure *structure;

  cv_type = gst_video_mark_gstfmt_to_cvtype (gst_video_fmt);
  const cv::Scalar &scalar = gst_video_mark_find_color (gst_video_fmt, video_mark->color);

  cv::Mat mat(height, width, cv_type, data);
  std::vector<std::string> result;
  while ((meta = gst_buffer_iterate_meta(frame->buffer, &state)) != NULL ) {
    if (meta->info->api != GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE) {
      continue;
    }

    roi_meta = (GstVideoRegionOfInterestMeta *) meta;
    if (roi_meta->w <= 0 || roi_meta->h <= 0) {
      continue;
    }

    if (roi_meta->x > 0 && roi_meta->y > 0) {
      cv::Point2f bbox_min(roi_meta->x, roi_meta->y);
      cv::Point2f bbox_max((roi_meta->x + roi_meta->w), (roi_meta->y + roi_meta->h));
      if (video_mark->label) {
        cv::putText(mat, std::string(video_mark->label), bbox_min, cv::FONT_HERSHEY_DUPLEX , video_mark->fontscale, scalar, 1, cv::LINE_AA);
      }
      cv::rectangle(mat, bbox_min, bbox_max, scalar, 1, cv::LINE_AA);
    }

    std::string res;
    for (GList *l = roi_meta->params; l; l = g_list_next(l)) {
      structure = (GstStructure *) l->data;
      if (gst_structure_has_field (structure, "label")) {
        res += std::string(gst_structure_get_string(structure, "label"));
      }

      res += " : ";
      if (gst_structure_has_field (structure, "confidence")) {
        double confidence = 0.0;
        gst_structure_get_double (structure, "confidence", &confidence);
        res += std::to_string(confidence);
      }
    }

    result.push_back(res);
  }

  for (unsigned int i = 0; i < result.size() && ((4 * (i * height / 20)) / 3) < (unsigned int)height; i++) {
    cv::Point2f bbox_result(((float)width / 4.0), ((float)height / 4.0) + (float) (i * height) / 20.0);
    cv::putText(mat, result[i], bbox_result, cv::FONT_HERSHEY_DUPLEX , video_mark->fontscale, scalar, 1, cv::LINE_AA);
  }

}

static gboolean
gst_video_mark_set_info (GstVideoFilter * vfilter, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstVideoMark *video_mark = GST_VIDEO_MARK (vfilter);

  GST_DEBUG_OBJECT (video_mark,
      "setting caps: in %" GST_PTR_FORMAT " out %" GST_PTR_FORMAT, incaps,
      outcaps);

  switch (GST_VIDEO_INFO_FORMAT (in_info)) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
      video_mark->process = gst_video_mark_ip;
      break;
    default:
      goto invalid_caps;
      break;
  }
  return TRUE;

  /* ERRORS */
invalid_caps:
  {
    GST_ERROR_OBJECT (video_mark, "Invalid caps: %" GST_PTR_FORMAT, incaps);
    return FALSE;
  }
}

static void
gst_video_mark_before_transform (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstVideoMark *video_mark = GST_VIDEO_MARK (base);
  GstClockTime timestamp, stream_time;

  timestamp = GST_BUFFER_TIMESTAMP (outbuf);
  stream_time =
      gst_segment_to_stream_time (&base->segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (video_mark, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (GST_OBJECT (video_mark), stream_time);
}

static GstFlowReturn
gst_video_mark_transform_frame_ip (GstVideoFilter * vfilter, GstVideoFrame * frame)
{
  GstVideoMark *video_mark = GST_VIDEO_MARK (vfilter);

  if (!video_mark->process)
    goto not_negotiated;

  GST_OBJECT_LOCK (video_mark);
  video_mark->process (video_mark, frame);
  GST_OBJECT_UNLOCK (video_mark);

  return GST_FLOW_OK;

  /* ERRORS */
not_negotiated:
  {
    GST_ERROR_OBJECT (video_mark, "Not negotiated yet");
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_video_mark_debug, "qtivideomark", 0, "QTI video mark plugin");

  return gst_element_register (plugin, "qtivideomark", GST_RANK_NONE,
      GST_TYPE_VIDEO_MARK);
}

GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtivideomark,
    "QTI Video Marker",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
