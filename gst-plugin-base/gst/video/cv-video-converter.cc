/*
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*
*    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
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

#include "cv-video-converter.h"

#include <unistd.h>
#include <dlfcn.h>
#include <cstdint>
#include <cmath>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>

// #define HAVE_DEBUG 1
#define OPENCV_IS_SUPPORTED_16FC3 1

#ifdef HAVE_DEBUG
#include <iostream>
#include <fstream>
#endif

#ifdef HAVE_DEBUG
#ifndef DEBUG_CONVERT_BIN_FILENAME
#define DEBUG_CONVERT_BIN_FILENAME "/data/raw.bin"
#endif
#endif

#ifdef OPENCV_IS_SUPPORTED_16FC3
#ifndef OPENCV_IS_SUPPORTED_16FC3
#define OPENCV_IS_SUPPORTED_16FC3 TRUE
#endif
#endif


#define GST_CV_RETURN_VAL_IF_FAIL(expression, value, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    return (value); \
  } \
}

#define GST_CV_RETURN_VAL_IF_FAIL_WITH_CLEAN(expression, value, cleanup, ...) \
{ \
  if (!(expression)) { \
    GST_ERROR (__VA_ARGS__); \
    cleanup; \
    return (value); \
  } \
}

#define EXTRACT_CV_RED_COLOR(color)   (((color >> 24) & 0xFF) / 255.0)
#define EXTRACT_CV_GREEN_COLOR(color) (((color >> 16) & 0xFF) / 255.0)
#define EXTRACT_CV_BLUE_COLOR(color)  (((color >> 8) & 0xFF) / 255.0)
#define EXTRACT_CV_ALPHA_COLOR(color) (((color) & 0xFF) / 255.0)

#define DEFAULT_OPT_OUTPUT_WIDTH     0
#define DEFAULT_OPT_OUTPUT_HEIGHT    0
#define DEFAULT_OPT_BACKGROUND       0x00000000
#define DEFAULT_OPT_RSCALE           1.0
#define DEFAULT_OPT_GSCALE           1.0
#define DEFAULT_OPT_BSCALE           1.0
#define DEFAULT_OPT_ASCALE           1.0
#define DEFAULT_OPT_QSCALE           1.0
#define DEFAULT_OPT_ROFFSET          0.0
#define DEFAULT_OPT_GOFFSET          0.0
#define DEFAULT_OPT_BOFFSET          0.0
#define DEFAULT_OPT_AOFFSET          0.0
#define DEFAULT_OPT_QOFFSET          0.0
#define DEFAULT_OPT_NORMALIZE        FALSE
#define DEFAULT_OPT_CONVERT          TRUE
#define DEFAULT_OPT_RESIZE           TRUE
#define DEFAULT_OPT_CONVERT_DATATYPE 0
#define DEFAULT_OPT_TRANSPOSE        0


#define GET_OPT_OUTPUT_WIDTH(s) get_opt_uint (s, \
    GST_CV_VIDEO_CONVERTER_OPT_OUTPUT_WIDTH, DEFAULT_OPT_OUTPUT_WIDTH)
#define GET_OPT_OUTPUT_HEIGHT(s) get_opt_uint (s, \
    GST_CV_VIDEO_CONVERTER_OPT_OUTPUT_HEIGHT, DEFAULT_OPT_OUTPUT_HEIGHT)
#define GET_OPT_RSCALE(s) get_opt_double (s, \
    GST_CV_VIDEO_CONVERTER_OPT_RSCALE, DEFAULT_OPT_RSCALE)
#define GET_OPT_GSCALE(s) get_opt_double (s, \
    GST_CV_VIDEO_CONVERTER_OPT_GSCALE, DEFAULT_OPT_GSCALE)
#define GET_OPT_BSCALE(s) get_opt_double (s, \
    GST_CV_VIDEO_CONVERTER_OPT_BSCALE, DEFAULT_OPT_BSCALE)
#define GET_OPT_ASCALE(s) get_opt_double (s, \
    GST_CV_VIDEO_CONVERTER_OPT_ASCALE, DEFAULT_OPT_ASCALE)
#define GET_OPT_ROFFSET(s) get_opt_double (s, \
    GST_CV_VIDEO_CONVERTER_OPT_ROFFSET, DEFAULT_OPT_ROFFSET)
#define GET_OPT_GOFFSET(s) get_opt_double (s, \
    GST_CV_VIDEO_CONVERTER_OPT_GOFFSET, DEFAULT_OPT_GOFFSET)
#define GET_OPT_BOFFSET(s) get_opt_double (s, \
    GST_CV_VIDEO_CONVERTER_OPT_BOFFSET, DEFAULT_OPT_BOFFSET)
#define GET_OPT_AOFFSET(s) get_opt_double (s, \
    GST_CV_VIDEO_CONVERTER_OPT_AOFFSET, DEFAULT_OPT_AOFFSET)
#define GET_OPT_NORMALIZE(s) get_opt_boolean (s, \
    GST_CV_VIDEO_CONVERTER_OPT_NORMALIZE, DEFAULT_OPT_NORMALIZE)
#define GET_OPT_DATATYPE(s) get_opt_uint (s, \
    GST_CV_VIDEO_CONVERTER_OPT_CONVERT_NETWORK_TYPE, DEFAULT_OPT_CONVERT_DATATYPE)
#define GST_OPT_BCONVERT(s)  get_opt_boolean (s, \
    GST_CV_VIDEO_CONVERTER_OPT_CONVERT, DEFAULT_OPT_CONVERT)
#define GST_OPT_BRESIZE(s)  get_opt_boolean (s, \
    GST_CV_VIDEO_CONVERTER_OPT_RESIZE, DEFAULT_OPT_RESIZE)
#define GST_OPT_BTRANSPOSE(s) get_opt_uint (s, \
    GST_CV_VIDEO_CONVERTER_OPT_TRANSPOSE, DEFAULT_OPT_TRANSPOSE)



#define GST_CV_GET_LOCK(obj) (&((GstCVConverter *)obj)->lock)
#define GST_CV_LOCK(obj)     g_mutex_lock (GST_CV_GET_LOCK(obj))
#define GST_CV_UNLOCK(obj)   g_mutex_unlock (GST_CV_GET_LOCK(obj))

#define GST_CAT_DEFAULT ensure_debug_category()

struct _GstCVEngineData
{
  // Data Mat
  cv::Mat mat;

  // actual width
  guint width;

  // actual height
  guint height;

  // video formattype _X_RGB etc
  GstVideoFormat format_type;

  // video cv data type, 8UC3 etc
  guint data_type;
};

struct _GstCVConverter
{
  // Global mutex lock.
  GMutex lock;

  // List of surface options for each input frame.
  GList *inopts;
  // Set of options performed for each output frame.
  GstStructure *outopts;

  // DataConverter to construct the converter pipeline
  // convert out size
  guint width, height;

  // convert out scale
  gfloat rscale, gscale, bscale, ascale;

  // convet out offset
  gfloat roffset, goffset, boffset, aoffset;

  // convert out data type
  guint datatype;

  // convert out if true, convert
  gboolean bconvert;

  // convert out if 0, transpose disable
  guint ntranspose;

  // convert out if true, resize
  gboolean bresize;
};

enum
{
  GST_CV_INPUT,
  GST_CV_OUTPUT,
};

static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("cv-video-converter",
        0, "OPENCV video converter");
    g_once_init_leave (&catonce, catdone);
  }

  return (GstDebugCategory *) catonce;
}

static gdouble
get_opt_double (const GstStructure * options, const gchar * opt, gdouble value)
{
  gdouble result;
  return gst_structure_get_double (options, opt, &result) ? result : value;
}

static gint
get_opt_uint (const GstStructure * options, const gchar * opt, guint value)
{
  guint result;
  return gst_structure_get_uint (options, opt, &result) ? result : value;
}

static gboolean
get_opt_boolean (const GstStructure * options, const gchar * opt, gboolean value)
{
  gboolean result;
  return gst_structure_get_boolean (options, opt, &result) ? result : value;
}

static gboolean
update_options (GQuark field, const GValue * value, gpointer userdata)
{
  gst_structure_id_set_value (GST_STRUCTURE_CAST (userdata), field, value);
  return TRUE;
}

#ifdef HAVE_DEBUG
static gboolean
debug_read_raw_image(void *src, std::string filename){
    std::ifstream fs(filename, std::ifstream::binary);
    fs.seekg(0, std::ios::end);
    guint32 length = fs.tellg();
    GST_TRACE("raw lenght = %d", length);
    fs.seekg(0, std::ios::beg);
    fs.read((gchar *)src, length);
    fs.close();
    return TRUE;
}
#endif

static gboolean
gst_video_format_to_cv_format(GstVideoFormat format,
                                              guint &type) {
  type = CV_8UC3;
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_I420:
      type = CV_8UC1;
      return TRUE;
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB:
      type = CV_8UC3;
      return TRUE;
    default:
      GST_ERROR("Unsupported format %s!", gst_video_format_to_string(format));
  }
  return FALSE;
}

static gint
gst_video_format_to_cv_datatype(gint format) {
  gint type = CV_8UC3;
  switch (format) {
    case GST_CV_UINT8:  // UINT8
      type = CV_8UC3;
      break;
    case GST_CV_INT8:  // INT8
      type = CV_8SC3;
      break;
#if OPENCV_IS_SUPPORTED_16FC3
    case GST_CV_FLOAT16: // FLOAT16
      type = CV_16FC3;
      break;
#endif
    case GST_CV_FLOAT32:  // FLOAT32
      type = CV_32FC3;
      break;
    default:
      GST_ERROR("Unsupported format datatype, default UINT8!");
      break;
  }
  return type;
}

static gboolean
gst_video_format_to_cv_color_convert_video(
    GstVideoFormat informat, GstVideoFormat outformat, guint &type) {
  switch (informat) {
    case GST_VIDEO_FORMAT_NV12:
      type = cv::COLOR_YUV2RGB_NV12;
      if (outformat == GST_VIDEO_FORMAT_BGR) type = cv::COLOR_YUV2BGR_NV12;
      return TRUE;
    case GST_VIDEO_FORMAT_I420:
      type = cv::COLOR_YUV2RGB_I420;
      if (outformat == GST_VIDEO_FORMAT_BGR) type = cv::COLOR_YUV2BGR_I420;
      return TRUE;
    case GST_VIDEO_FORMAT_BGR:
      type = cv::COLOR_BGR2RGB;
      if (outformat == GST_VIDEO_FORMAT_BGR) type = cv::COLOR_COLORCVT_MAX;
      return TRUE;
    case GST_VIDEO_FORMAT_RGB:
      type = cv::COLOR_COLORCVT_MAX;
      if (outformat == GST_VIDEO_FORMAT_BGR) type = cv::COLOR_RGB2BGR;
      return TRUE;
    default:
      GST_ERROR("Unsupported format %s!", gst_video_format_to_string(informat));
  }
  return FALSE;
}

static gboolean
gst_video_format_to_cv_convert_h_w(GstVideoFormat format,
                                                   guint &height,
                                                   guint &width) {
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_I420:
      height = height * 3 / 2;
      width = width;
      return TRUE;
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB:
      return TRUE;
    default:
      GST_ERROR("Unsupported format %s!", gst_video_format_to_string(format));
  }

  GST_TRACE("Image(colortformat=%d) to Mat.Size, height=%d, width=%d", format,
            height, width);
  return FALSE;
}

static void
gst_video_fromat_to_cv_convert_roi_h_w(guint &c_y, guint &c_x,
                                                   guint &c_h, guint &c_w,
                                                   cv::Rect rt,
                                                   GstVideoFormat format_type) {
  c_x = rt.x;
  c_y = rt.y;
  c_w = rt.width;
  c_h = rt.height;
  gst_video_format_to_cv_convert_h_w(format_type, c_y, c_x);
  gst_video_format_to_cv_convert_h_w(format_type, c_h, c_w);
}

template <typename T>
static gboolean
gst_video_format_to_cv_convert_nhwc2nchw(T *dst, T *src, int w,
                                                 int h, uint8_t c) {
  T *s = (T *)src;
  T *d = (T *)dst;
  for (int i = 0; i < h; i++)
    for (int j = 0; j < w; j++)
      for (int k = 0; k < c; k++)
        d[k * w * h + i * w + j] = s[i * w * c + j * c + k];
  return 0;
}

static gboolean
gst_cv_video_converter_update_image(
    GstCVConverter *convert, GstCVEngineData &image, const guint direction,
    const GstVideoFrame *frame) {
  GstMapInfo mapinfo;
  GstMapFlags mapflags;
  GstVideoFormat format;
  guint width, height;
  guint colortype;

  g_return_val_if_fail(convert != NULL, FALSE);

  if (direction == GST_CV_INPUT) {
    mapflags = GST_MAP_READ;
  } else {
    mapflags = GST_MAP_READWRITE;
  }
  format = GST_VIDEO_FRAME_FORMAT(frame);

  gst_buffer_map(frame->buffer, &mapinfo, mapflags);

  width = GST_VIDEO_FRAME_WIDTH(frame);
  height = GST_VIDEO_FRAME_HEIGHT(frame);
  image.width = width;
  image.height = height;

  if (!gst_video_format_to_cv_convert_h_w(format, height, width)) {
    GST_ERROR("Unsupported format!");
    return FALSE;
  }

  if (direction == GST_CV_INPUT) {
    if (!gst_video_format_to_cv_format(format, colortype)) {
      GST_ERROR("Unsupported format!");
      return FALSE;
    }
  } else {
    // default convert out colorspace is RGB/BGR,
    colortype = gst_video_format_to_cv_datatype(convert->datatype);
  }

  image.mat = cv::Mat(cv::Size(width, height), colortype, mapinfo.data);
  image.data_type = colortype;
  image.format_type = format;

  // clear out image buffer
  if (direction == GST_CV_OUTPUT) {
    image.mat = 0;
  }

  gst_buffer_unmap(frame->buffer, &mapinfo);

  return TRUE;
}

static void
gst_cv_video_converter_extract_rectangles (const GstStructure * opts,
    const gchar * opt, std::vector<cv::Rect> &rectangles)
{
  const GValue *entries = NULL, *entry = NULL;
  const gchar *type = NULL;
  guint idx = 0, n_entries = 0, n_rects = 0;

  entries = gst_structure_get_value (opts, opt);
  n_entries = (entries == NULL) ? 0 : gst_value_array_get_size (entries);

  // Make sure that there is at least one new rectangle in the list.
  n_rects = (n_entries == 0) ? 1 : n_entries;

  type = (g_strcmp0 (opt, GST_CV_VIDEO_CONVERTER_OPT_SRC_RECTANGLES) == 0) ?
      "Source" : "Destination";

  // Make sure that there is at least one new rectangle in the list.
  for (idx = 0; idx < n_rects; idx++) {
    cv::Rect rectangle = {0,0,0,0};

    entry = (n_entries != 0) ? gst_value_array_get_value (entries, idx) : NULL;

    if ((entry != NULL) && (gst_value_array_get_size (entry) == 4)) {
      rectangle.x = g_value_get_int (gst_value_array_get_value (entry, 0));
      rectangle.y = g_value_get_int (gst_value_array_get_value (entry, 1));
      rectangle.width = g_value_get_int (gst_value_array_get_value (entry, 2));
      rectangle.height = g_value_get_int (gst_value_array_get_value (entry, 3));
      GST_TRACE("Set rectangle type(%s)(%d,%d,%d,%d)", type, rectangle.x, rectangle.y,
                                                     rectangle.width, rectangle.height);
    } else if (entry != NULL) {
      GST_WARNING ("%s rectangle at index %u does not contain exactly 4"
          "values, using default values!", type, idx);
    }

    rectangles.push_back(rectangle);
  }
}

static gboolean
gst_cv_video_converter_do_pre_process(
    GstCVConverter *convert, GstCVEngineData in_image,
    GstCVEngineData &out_image) {

  g_return_val_if_fail(convert != NULL, FALSE);

  guint resize_w = out_image.width;
  guint resize_h = out_image.height;

  cv::Mat resize_img;
  cv::Mat convert_img;
  guint data_type = out_image.data_type;

  //out_image.mat = 0;
  guint convert_type = cv::COLOR_BGR2RGB;
  // GST color space convert to OPNECV type,
  if (!gst_video_format_to_cv_color_convert_video(
          in_image.format_type, out_image.format_type, convert_type)) {
    GST_ERROR("Unsupported format!");
    return FALSE;
  }

  // if color space is same, just to resize
  if (convert->bconvert && convert_type != cv::COLOR_COLORCVT_MAX) {
    // enable convert
    cv::cvtColor(in_image.mat, convert_img, convert_type);
  } else {
    convert_img = in_image.mat;
  }

  if (convert->bresize && ((guint)convert_img.cols != resize_w ||
                           (guint)convert_img.rows != resize_h)) {
    cv::resize(convert_img, resize_img, cv::Size(resize_w, resize_h));
  } else {
    resize_img = convert_img;
  }

  resize_img.convertTo(out_image.mat(cv::Rect(0, 0, resize_w, resize_h)),
                         data_type);

  GST_TRACE("Rect Mat calibration: %d, %d\n", resize_h, resize_w);

  // if true. Mat nhwc will to nchw
  if (convert->ntranspose == 2) {
    cv::Mat nchw_img = out_image.mat(cv::Rect(0, 0, resize_w, resize_h));
    cv::Mat nhwc_img = nchw_img.clone();
    if (data_type == CV_32FC3
#if OPENCV_IS_SUPPORTED_16FC3
                || data_type == CV_16FC3
#endif
    ) {
      gst_video_format_to_cv_convert_nhwc2nchw<float>(
          (float *)nchw_img.data, (float *)nhwc_img.data, resize_w, resize_h, 3);
    } else if (data_type == CV_8SC3 || data_type == CV_8UC3) {
      gst_video_format_to_cv_convert_nhwc2nchw<guchar>(
          (guchar *)nchw_img.data, (guchar *)nhwc_img.data, resize_w, resize_h, 3);
    }
  }

  cv::Mat norm_mat = out_image.mat(cv::Rect(0, 0, resize_w, resize_h));

  cv::Scalar meansc(convert->roffset, convert->goffset, convert->boffset);
  cv::Scalar stdsc(convert->rscale, convert->gscale, convert->bscale);
  if (data_type == CV_32FC3) {
    // meansc = cv::Scalar(0.44, 0.44, 0.44);
    // stdsc = cv::Scalar(0.23, 0.23, 0.23);
    cv::Mat mean(resize_h, resize_w, data_type, meansc);
    cv::Mat std(resize_h, resize_w, data_type, stdsc);

    const float scale_normal = 1.0 / 255.0;
    norm_mat = norm_mat * scale_normal;
    norm_mat = norm_mat - mean;
    norm_mat = norm_mat / std;
#if OPENCV_IS_SUPPORTED_16FC3
    } else if (data_type == CV_16FC3) {
      const float scale_normal = 1.0 / 255.0;
      norm_mat = norm_mat * scale_normal;
#endif
  } else if (data_type == CV_8SC3) {
    // meansc = cv::Scalar(128, 128, 128);
    cv::Mat mean(resize_h, resize_w, data_type, meansc);
    norm_mat = norm_mat - mean;
  } else if (data_type == CV_8UC3) {
    // Nothing to do, when is UINT8
  }
  return TRUE;
}

GstCVConverter *
gst_cv_video_converter_new ()
{
  GstCVConverter *convert = NULL;

  convert = g_slice_new0 (GstCVConverter);
  g_return_val_if_fail (convert != NULL, NULL);

  g_mutex_init (&convert->lock);

  convert->outopts = gst_structure_new_empty ("Output");
  GST_CV_RETURN_VAL_IF_FAIL_WITH_CLEAN (convert->outopts != NULL, NULL,
      gst_cv_video_converter_free (convert), "Failed to create OPTS struct!");

  GST_INFO ("Created opencv Converter %p", convert);
  return convert;
}

void
gst_cv_video_converter_free (GstCVConverter * convert)
{
  if (convert == NULL)
    return;

  if (convert->inopts != NULL)
    g_list_free_full (convert->inopts, (GDestroyNotify) gst_structure_free);

  if (convert->outopts != NULL)
    gst_structure_free (convert->outopts);

  g_mutex_clear (&convert->lock);

  GST_INFO ("Destroyed opencv converter: %p", convert);
  g_slice_free (GstCVConverter, convert);
}

gboolean
gst_cv_video_converter_set_input_opts (GstCVConverter * convert,
    guint index, GstStructure *opts)
{
  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (opts != NULL, FALSE);

  // Locking the converter to set the opts and composition pipeline
  GST_CV_LOCK (convert);

  if (index > g_list_length (convert->inopts)) {
    GST_ERROR ("Provided index %u is not sequential!", index);
    GST_CV_UNLOCK (convert);
    return FALSE;
  } else if ((index == g_list_length (convert->inopts)) && (NULL == opts)) {
    GST_DEBUG ("There is no configuration for index %u", index);
    GST_CV_UNLOCK (convert);
    return TRUE;
  } else if ((index < g_list_length (convert->inopts)) && (NULL == opts)) {
    GST_LOG ("Remove options from the list at index %u", index);
    convert->inopts = g_list_remove (convert->inopts,
        g_list_nth_data (convert->inopts, index));
    GST_CV_UNLOCK (convert);
    return TRUE;
  }

  if (index == g_list_length (convert->inopts)) {
    GST_LOG ("Add a new opts structure in the list at index %u", index);

    convert->inopts = g_list_append (convert->inopts,
        gst_structure_new_empty ("Input"));
  }

  // Iterate over the fields in the new opts structure and update them.
  gst_structure_foreach (opts, update_options,
      g_list_nth_data (convert->inopts, index));
  gst_structure_free (opts);

  GST_CV_UNLOCK (convert);

  return TRUE;
}

gboolean
gst_cv_video_converter_set_output_opts (GstCVConverter * convert,
    GstStructure * opts)
{
  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (opts != NULL, FALSE);

  // Locking the converter to set the opts and composition pipeline
  GST_CV_LOCK(convert);

  // Iterate over the fields in the new opts structure and update them.
  gst_structure_foreach (opts, update_options, convert->outopts);
  gst_structure_free (opts);

  convert->width  = GET_OPT_OUTPUT_WIDTH (convert->outopts);
  convert->height = GET_OPT_OUTPUT_HEIGHT (convert->outopts);

  convert->rscale  = GET_OPT_RSCALE (convert->outopts);
  convert->gscale  = GET_OPT_GSCALE (convert->outopts);
  convert->bscale  = GET_OPT_BSCALE (convert->outopts);
  convert->ascale  = GET_OPT_ASCALE (convert->outopts);

  convert->roffset = GET_OPT_ROFFSET (convert->outopts);
  convert->goffset = GET_OPT_GOFFSET (convert->outopts);
  convert->boffset = GET_OPT_BOFFSET (convert->outopts);
  convert->aoffset = GET_OPT_AOFFSET (convert->outopts);

  convert->datatype = GET_OPT_DATATYPE (convert->outopts);
  convert->bconvert = GST_OPT_BCONVERT(convert->outopts);
  convert->bresize = GST_OPT_BRESIZE(convert->outopts);
  convert->ntranspose = GST_OPT_BTRANSPOSE(convert->outopts);


  GST_CV_UNLOCK (convert);

  if (convert->width == 0 || convert->height == 0) {
    GST_ERROR ("Invalid output dimensions: %ux%u!",
                      convert->width, convert->height);
    return FALSE;
  }

  GST_DEBUG ("Resize dimensions: %ux%u", convert->width, convert->height);

  return TRUE;
}

gboolean
gst_cv_video_converter_process (GstCVConverter * convert,
    GstVideoFrame * inframes, guint n_inputs, GstVideoFrame * outframes,
    guint n_outputs)
{
  std::vector<GstCVEngineData> inimages, outimages;
  std::vector<cv::Rect> srcrects, dstrects;
  gboolean success = FALSE;
  guint idx = 0, num = 0, n_rects = 0;

  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail ((inframes != NULL) && (n_inputs != 0), FALSE);
  g_return_val_if_fail ((outframes != NULL) && (n_outputs != 0), FALSE);

  GST_CV_LOCK (convert);

  for (idx = 0; idx < n_inputs; idx++) {
    const GstVideoFrame *frame = &inframes[idx];
    const GstStructure *opts = NULL;
    GstCVEngineData image;

    if (NULL == frame->buffer)
      continue;

    // Initialize empty options structure in case none have been set.
    if (idx >= g_list_length (convert->inopts))
      convert->inopts = g_list_append (convert->inopts,
          gst_structure_new_empty ("Input"));

    // Get the options for current input buffer.
    opts = GST_STRUCTURE (g_list_nth_data (convert->inopts, idx));

    success = gst_cv_video_converter_update_image (convert, image, GST_CV_INPUT, frame);
    GST_CV_RETURN_VAL_IF_FAIL_WITH_CLEAN (success, FALSE, GST_CV_UNLOCK (convert),
        "Failed to update MatData image at index %u !", idx);

    // Set the start index to the number of initial rectangles.
    num = n_rects = dstrects.size();

    // Fill the source and destination rectangles.
    gst_cv_video_converter_extract_rectangles (opts,
        GST_CV_VIDEO_CONVERTER_OPT_SRC_RECTANGLES, srcrects);
    gst_cv_video_converter_extract_rectangles (opts,
        GST_CV_VIDEO_CONVERTER_OPT_DEST_RECTANGLES, dstrects);

    if (srcrects.size() > dstrects.size()) {
      GST_WARNING ("Number of source rectangles exceeds the number of "
          "destination rectangles, clipping!");
      n_rects = dstrects.size();
      srcrects.resize(n_rects);
    } else if (srcrects.size() < dstrects.size()) {
      GST_WARNING ("Number of destination rectangles exceeds the number of "
          "source rectangles, clipping!");
      n_rects = srcrects.size();
      dstrects.resize(n_rects);
    }

    n_rects = srcrects.size();

    // Iterate over the pairs of source and destination rectangles.
    while (num < n_rects) {
      // Use the same image for each pair of source and destination rectangles.
      inimages.push_back(image);

      if ((srcrects[num].width == 0) && (srcrects[num].height == 0)) {
        srcrects[num].x = srcrects[num].y = 0;
        srcrects[num].width = image.width;
        srcrects[num].height = image.height;
      }

      GST_TRACE ("Input image FD[inputn-%d] - Source rectangle[%u]: [%u %u %u %u]",
          n_inputs, num, srcrects[num].x, srcrects[num].y, srcrects[num].width,
          srcrects[num].height);

      if ((dstrects[num].width == 0) && (dstrects[num].height == 0)) {
        dstrects[num].x = 0;
        dstrects[num].y = idx * GET_OPT_OUTPUT_HEIGHT (convert->outopts) / n_inputs;
        dstrects[num].width = GET_OPT_OUTPUT_WIDTH (convert->outopts);
        dstrects[num].height = GET_OPT_OUTPUT_HEIGHT (convert->outopts) / n_inputs;
      }

      GST_TRACE ("Input image FD[inputn-%d] - Target rectangle[%u]: [%u %u %u %u]",
          n_inputs, num, dstrects[num].x, dstrects[num].y, dstrects[num].width,
          dstrects[num].height);

      // Increment the rectangles index.
      num++;
    }
  }

  for (idx = 0; idx < n_outputs; idx++) {
    const GstVideoFrame *frame = &outframes[idx];
    GstCVEngineData image;

    if (NULL == frame->buffer)
      continue;

    success = gst_cv_video_converter_update_image(convert, image, GST_CV_OUTPUT, frame);
    GST_CV_RETURN_VAL_IF_FAIL_WITH_CLEAN(success, FALSE, GST_CV_UNLOCK(convert),
                                         "Failed to update output image at index %u !", idx);
    outimages.push_back(image);
  }

  guint ini_size = inimages.size();
  guint out_size = outimages.size();
  gdouble ceil_idx = std::ceil(inimages.size() / outimages.size());
  GST_TRACE("convert(%p) ini_size=%d, out_size=%d, ceil_idx=%f", convert,
            ini_size, out_size, ceil_idx);
  for (idx = 0; idx < ini_size; idx++) {
    GstCVEngineData in_image;
    GstCVEngineData out_image;
    guint c_x, c_y, c_h, c_w;
    in_image = inimages[idx];
    in_image.width = srcrects[idx].width;
    in_image.height = srcrects[idx].height;
    gst_video_fromat_to_cv_convert_roi_h_w(c_y, c_x, c_h, c_w, srcrects[idx],
                                           in_image.format_type);
    // ROI mat,  support RGB,BGR
    in_image.mat = in_image.mat(cv::Rect(c_x, c_y, c_w, c_h));

    if (ceil_idx <= 1)
      out_image = outimages[idx];
    else
      out_image = outimages[0];
    out_image.width = dstrects[idx].width;
    out_image.height = dstrects[idx].height;
    gst_video_fromat_to_cv_convert_roi_h_w(c_y, c_x, c_h, c_w, dstrects[idx],
                                           out_image.format_type);
    // ROI mat,  support RGB,BGR
    out_image.mat = out_image.mat(cv::Rect(c_x, c_y, c_w, c_h));

    // alg pre process
    gst_cv_video_converter_do_pre_process(convert, in_image, out_image);
#ifdef HAVE_DEBUG
    debug_read_raw_image(out_image.mat.data, DEBUG_CONVERT_BIN_FILENAME);
#endif
  }

  GST_CV_UNLOCK (convert);

  GST_CV_RETURN_VAL_IF_FAIL (success, FALSE,
      "Failed to process frames!");
  return TRUE;
}
