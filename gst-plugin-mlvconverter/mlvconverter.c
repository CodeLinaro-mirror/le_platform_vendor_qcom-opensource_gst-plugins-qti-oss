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
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
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

#include "mlvconverter.h"

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>

#include <gst/ml/gstmlpool.h>
#include <gst/ml/gstmlmeta.h>
#include <gst/video/gstimagepool.h>


#define GST_CAT_DEFAULT gst_ml_video_converter_debug
GST_DEBUG_CATEGORY_STATIC (gst_ml_video_converter_debug);

#define gst_ml_video_converter_parent_class parent_class
G_DEFINE_TYPE (GstMLVideoConverter, gst_ml_video_converter,
    GST_TYPE_BASE_TRANSFORM);

#define GST_BATCH_CHANNEL_BASE       100

#define DEFAULT_PROP_MIN_BUFFERS     4
#define DEFAULT_PROP_MAX_BUFFERS     4

#define DEFAULT_PROP_SUBPIXEL_LAYOUT GST_ML_VIDEO_PIXEL_LAYOUT_REGULAR
#define DEFAULT_PROP_MEAN            0.0
#define DEFAULT_PROP_SIGMA           1.0
#define DEFAULT_PROP_CONVERT         TRUE
#define DEFAULT_PROP_RESIZE          TRUE
#define DEFAULT_PROP_ASPECTION_RATION   TRUE
#define DEFAULT_PROP_TRANSPOSE       GST_ML_VIDEO_TRANSPOSE_UNKNOWN

#define GET_MEAN_VALUE(mean, idx) (mean->len >= (guint) (idx + 1)) ? \
    g_array_index (mean, gdouble, idx) : DEFAULT_PROP_MEAN
#define GET_SIGMA_VALUE(sigma, idx) (sigma->len >= (guint) (idx + 1)) ? \
    g_array_index (sigma, gdouble, idx) : DEFAULT_PROP_SIGMA

#define GST_PROTECTION_META_CAST(obj) ((GstProtectionMeta *) obj)


#define GST_ML_VIDEO_FORMATS \
    "{ I420, BGR, RGB, NV12 }"

#define GST_ML_VIDEO_CONVERTER_SINK_CAPS                          \
    "video/x-raw, "                                               \
    "format = (string) " GST_ML_VIDEO_FORMATS

#define GST_ML_TENSOR_TYPES "{ UINT8, INT8, FLOAT16, FLOAT32 }"

#define GST_ML_VIDEO_CONVERTER_SRC_CAPS    \
    "neural-network/tensors, "             \
    "type = (string) " GST_ML_TENSOR_TYPES

enum
{
  PROP_0,
  PROP_SUBPIXEL_LAYOUT,
  PROP_MEAN,
  PROP_SIGMA,
  PROP_CONVERT,
  PROP_RESIZE,
  PROP_TRANSPOSE,
  PROP_ASPECT_RATION,
};

static GstStaticCaps gst_ml_video_converter_static_sink_caps =
    GST_STATIC_CAPS (GST_ML_VIDEO_CONVERTER_SINK_CAPS);

static GstStaticCaps gst_ml_video_converter_static_src_caps =
    GST_STATIC_CAPS (GST_ML_VIDEO_CONVERTER_SRC_CAPS);


static GstCaps *
gst_ml_video_converter_sink_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_video_converter_static_sink_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstCaps *
gst_ml_video_converter_src_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;

  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_ml_video_converter_static_src_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

static GstPadTemplate *
gst_ml_video_converter_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_ml_video_converter_sink_caps ());
}

static GstPadTemplate *
gst_ml_video_converter_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_ml_video_converter_src_caps ());
}

GType
gst_ml_video_pixel_layout_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue variants[] = {
    { GST_ML_VIDEO_PIXEL_LAYOUT_REGULAR,
        "Regular subpixel layout e.g. RGB, RGBA, RGBx, etc.", "regular"
    },
    { GST_ML_VIDEO_PIXEL_LAYOUT_REVERSE,
        "Reverse subpixel layout e.g. BGR, BGRA, BGRx, etc.", "reverse"
    },
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstMLVideoPixelLayout", variants);

  return gtype;
}

GType
gst_ml_video_transpose_get_type (void)
{
  static GType gtype = 0;

  static const GEnumValue variants[] = {
    { GST_ML_VIDEO_TRANSPOSE_UNKNOWN,
        "Disable transpose.", "UNKNOWN"
    },
    { GST_ML_VIDEO_TRANSPOSE_NHWC,
        "Combined with AI model format requirements, Batch, Height, Width, Channel.", "NHWC"
    },
    { GST_ML_VIDEO_TRANSPOSE_NCHW,
        "Combined with AI model format requirements, Batch, Channel, Height, Width.", "NCHW"
    },
    { 0, NULL, NULL },
  };

  if (!gtype)
    gtype = g_enum_register_static ("GstMLVideoTranspose", variants);

  return gtype;
}

static void
init_formats (GValue * formats, ...)
{
  GValue format = G_VALUE_INIT;
  gchar *string = NULL;
  va_list args;

  g_value_init (formats, GST_TYPE_LIST);
  va_start (args, formats);

  while ((string = va_arg (args, gchar *))) {
    g_value_init (&format, G_TYPE_STRING);
    g_value_set_string (&format, string);

    gst_value_list_append_value (formats, &format);
    g_value_unset (&format);
  }

  va_end (args);
}

static gboolean
is_conversion_required (GstVideoFrame * inframe, GstVideoFrame * outframe)
{
  gboolean conversion = FALSE;

  // Conversion is required if input and output formats are different.
  conversion |=  GST_VIDEO_FRAME_FORMAT (inframe) !=
      GST_VIDEO_FRAME_FORMAT (outframe);
  // Conversion is required if input and output strides are different.
  conversion |= GST_VIDEO_FRAME_PLANE_STRIDE (inframe, 0) !=
      GST_VIDEO_FRAME_PLANE_STRIDE (outframe, 0);
  // Conversion is required if input and output heights are different.
  conversion |= GST_VIDEO_FRAME_HEIGHT (inframe) !=
      GST_VIDEO_FRAME_HEIGHT (outframe);

  return conversion;
}

static gboolean
is_normalization_required (GstMLInfo * mlinfo)
{
  return (mlinfo->type == GST_ML_TYPE_INT8) ||
      (mlinfo->type == GST_ML_TYPE_FLOAT16) ||
      (mlinfo->type == GST_ML_TYPE_FLOAT32);
}

static guint
mlinfo_network_neural_data_type_serial (GstMLInfo * mlinfo)
{
  guint type = 0;
  switch (mlinfo->type)
  {
    case GST_ML_TYPE_UINT8:
      type = GST_CV_UINT8;
      break;
    case GST_ML_TYPE_INT8:
      type = GST_CV_INT8;
      break;
    case GST_ML_TYPE_FLOAT16:
      type = GST_CV_FLOAT16;
      break;
    case GST_ML_TYPE_FLOAT32:
      type = GST_CV_FLOAT32;
      break;
    default:
      break;
  }
  GST_TRACE("Check the received type is %d, MLINFO(%d)", type, mlinfo->type);
  return type;
}

static void
calculate_dimensions (gint outwidth, gint outheight, gint out_par_n,
    gint out_par_d, gint sar_n, gint sar_d, gint * width, gint * height)
{
  gint num = 0, den = 0;

  gst_util_fraction_multiply (sar_n, sar_d, out_par_d, out_par_n, &num, &den);

  if (num > den) {
    *width = outwidth;
    *height = gst_util_uint64_scale_int (outwidth, den, num);
  } else if (num < den) {
    *width = gst_util_uint64_scale_int (outheight, num, den);
    *height = outheight;
  }
}

static GstProtectionMeta *
gst_buffer_get_protection_meta_id (GstBuffer * buffer, const gchar * name)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;

  while ((meta = gst_buffer_iterate_meta_filtered (buffer, &state,
              GST_PROTECTION_META_API_TYPE))) {
    if (gst_structure_has_name (GST_PROTECTION_META_CAST (meta)->info, name))
      return GST_PROTECTION_META_CAST (meta);
  }

  return NULL;
}

static void
gst_unmap_input_video_frames (GstVideoFrame * inframes, guint n_inputs)
{
  GstBuffer *buffer = NULL;
  guint idx = 0;

  for (idx = 0; idx < n_inputs; idx++) {
    if ((buffer = inframes[idx].buffer) == NULL)
      continue;

    gst_video_frame_unmap (&inframes[idx]);
    gst_buffer_unref (buffer);
  }

  g_free (inframes);
}

static gboolean
gst_map_input_video_frames (GstVideoFrame ** inframes, guint n_inputs,
    GstVideoInfo * info, GstBuffer * inbuffer, GstMapFlags flags)
{
  GstVideoFrame *frames = NULL;
  guint idx = 0, num = 0, id = 0, n_memory = 0;

  if ((n_memory = gst_buffer_n_memory (inbuffer)) > n_inputs) {
    GST_ERROR ("Number of memory blocks (%u) exceeds the batch size (%u)!",
        n_memory, n_inputs);
    return FALSE;
  }

  frames = g_new0 (GstVideoFrame, n_inputs);

  for (idx = 0, num = 0; idx < n_inputs; idx++) {
    GstBuffer *buffer = NULL;
    GstVideoMeta *vmeta = NULL;
    GstVideoRegionOfInterestMeta *roimeta = NULL;

    // Check if a bitwise mask was set for this channel/batch input.
    if ((GST_BUFFER_OFFSET (inbuffer) != GST_BUFFER_OFFSET_NONE)) {
      if ((GST_BUFFER_OFFSET_END (inbuffer) == GST_BUFFER_OFFSET_NONE) &&
        ((GST_BUFFER_OFFSET (inbuffer) & (1 << idx)) == 0)) {
        continue;
      } else if ((GST_BUFFER_OFFSET_END (inbuffer) != GST_BUFFER_OFFSET_NONE)) {
        GST_TRACE ("n_inputs = %u, offset = %lu, offset_end = %lu", n_inputs,
          GST_BUFFER_OFFSET (inbuffer), GST_BUFFER_OFFSET_END (inbuffer));
        GST_BUFFER_OFFSET (inbuffer) = 1;
      }
    }

    // Check if there is memory block for this index.
    if (num >= n_memory)
      break;

    // Create a new buffer to placehold a reference to a single GstMemory block.
    buffer = gst_buffer_new ();

    // Append the memory block from input buffer into the new buffer.
    gst_buffer_append_memory (buffer, gst_buffer_get_memory (inbuffer, num));

    // Add parent meta, input buffer won't be released until new buffer is freed.
    gst_buffer_add_parent_buffer_meta (buffer, inbuffer);

    // Copy video metadata for current memory block into the new buffer.
    if ((vmeta = gst_buffer_get_video_meta_id (inbuffer, num)) != NULL)
      gst_buffer_add_video_meta_full (buffer, GST_VIDEO_FRAME_FLAG_NONE,
          vmeta->format, vmeta->width, vmeta->height, vmeta->n_planes,
          vmeta->offset, vmeta->stride);

    id = idx * GST_BATCH_CHANNEL_BASE;
    roimeta = gst_buffer_get_video_region_of_interest_meta_id (inbuffer, id);

    // Copy ROI metadata for current memory block into the new buffer.
    while (roimeta != NULL) {
      roimeta = gst_buffer_add_video_region_of_interest_meta_id (buffer,
          roimeta->roi_type, roimeta->x, roimeta->y, roimeta->w, roimeta->h);
      roimeta->id = id % GST_BATCH_CHANNEL_BASE;

      roimeta = gst_buffer_get_video_region_of_interest_meta_id (inbuffer, ++id);
    }

    if (!gst_video_frame_map (&frames[idx], info, buffer, flags)) {
      GST_ERROR ("Failed to map frame at idx %u!", idx);

      gst_buffer_unref (buffer);
      gst_unmap_input_video_frames (frames, n_inputs);

      return FALSE;
    }

    num++;
  }

  *inframes = frames;
  if (num <= 0 ) {
    GST_ERROR ("Failed to map frame sum nume is %u!", num);
    return FALSE;
  }
  return TRUE;
}

static void
gst_ml_video_converter_update_params (GstMLVideoConverter * mlconverter,
    GstVideoFrame * inframes, guint n_inputs, GstVideoFrame * outframe)
{
  GstStructure *structure = NULL;

  GValue srcrects = G_VALUE_INIT, dstrects = G_VALUE_INIT;
  GValue entry = G_VALUE_INIT, value = G_VALUE_INIT;
  GstVideoRectangle inrect = {0,0,0,0}, outrect = {0,0,0,0};
  guint idx = 0, num = 0, id = 0, n_entries = 0, maxwidth = 0, maxheight = 0;
  gint par_n = 0, par_d = 0, sar_n = 0, sar_d = 0;

  g_value_init (&srcrects, GST_TYPE_ARRAY);
  g_value_init (&dstrects, GST_TYPE_ARRAY);

  g_value_init (&entry, GST_TYPE_ARRAY);
  g_value_init (&value, G_TYPE_INT);

  // Fill the maximum width and height of destination rectangles.
  maxwidth = GST_VIDEO_FRAME_WIDTH (outframe);
  maxheight = GST_VIDEO_FRAME_HEIGHT  (outframe) / n_inputs;

  // Fill output PAR (Pixel Aspect Ratio), will be used to calculations.
  par_n = GST_VIDEO_INFO_PAR_N (&(outframe)->info);
  par_d = GST_VIDEO_INFO_PAR_D (&(outframe)->info);

  // Iterate over all input frames.
  for (idx = 0, id = 0; idx < n_inputs; idx++, id++) {
    GstVideoFrame *inframe = &inframes[idx];
    GstProtectionMeta *pmeta = NULL;
    gchar *name = NULL;

    // There is no buffer for this frame, no need to update params for it.
    if (inframe->buffer == NULL)
      continue;

    n_entries = gst_buffer_get_n_meta (inframe->buffer,
        GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);

    // Have at least 1 entry pair of source/destination rectangles..
    n_entries = (n_entries == 0) ? 1 : n_entries;

    for (num = 0; num < n_entries; num++) {
      GstVideoRegionOfInterestMeta *roimeta =
          gst_buffer_get_video_region_of_interest_meta_id (inframe->buffer, num);

      // If available extract coordinates and dimensions from ROI meta.
      inrect.x = roimeta ? (gint) roimeta->x : 0;
      inrect.y = roimeta ? (gint) roimeta->y : 0;
      inrect.w = roimeta ? (gint) roimeta->w : GST_VIDEO_FRAME_WIDTH (inframe);
      inrect.h = roimeta ? (gint) roimeta->h : GST_VIDEO_FRAME_HEIGHT (inframe);

      g_value_set_int (&value, inrect.x);
      gst_value_array_append_value (&entry, &value);
      g_value_set_int (&value, inrect.y);
      gst_value_array_append_value (&entry, &value);
      g_value_set_int (&value, inrect.w);
      gst_value_array_append_value (&entry, &value);
      g_value_set_int (&value, inrect.h);
      gst_value_array_append_value (&entry, &value);

      gst_value_array_append_value (&srcrects, &entry);
      g_value_reset (&entry);

      // Calculate input SAR (Source Aspect Ratio) value.
      if (!gst_util_fraction_multiply (inrect.w, inrect.h, par_n, par_d,
              &sar_n, &sar_d))
        sar_n = sar_d = 1;

      // Calculate the Y offset for this ROI meta in the output buffer.
      outrect.y = (id + num) * maxheight;
      outrect.x = 0;

      // Calculate destination dimensions adjusted to preserve SAR.
      calculate_dimensions (maxwidth, maxheight, par_n, par_d, sar_n, sar_d,
          &outrect.w, &outrect.h);

      if (!mlconverter->aspect_ration) {
        outrect.w = maxwidth;
        outrect.h = maxheight;
      }

      g_value_set_int (&value, outrect.x);
      gst_value_array_append_value (&entry, &value);
      g_value_set_int (&value, outrect.y);
      gst_value_array_append_value (&entry, &value);
      g_value_set_int (&value, outrect.w);
      gst_value_array_append_value (&entry, &value);
      g_value_set_int (&value, outrect.h);
      gst_value_array_append_value (&entry, &value);

      gst_value_array_append_value (&dstrects, &entry);
      g_value_reset (&entry);

      // Construct the name for the protection meta structure.
      name = g_strdup_printf ("channel-%u", (id + num));

      // Add channel protection meta to the output buffer if not available.
      if (!(pmeta = gst_buffer_get_protection_meta_id (outframe->buffer, name)))
        pmeta = gst_buffer_add_protection_meta (outframe->buffer,
            gst_structure_new_empty (name));

      // Add SAR information for tensor decryption downstream.
      gst_structure_set (pmeta->info,
          "source-aspect-ratio", GST_TYPE_FRACTION, sar_n, sar_d, NULL);
      g_free (name);

      GST_TRACE_OBJECT (mlconverter, "Rectangles [%u] SAR[%d/%d]: [%d %d %d %d]"
          " -> [%d %d %d %d]", idx, sar_n, sar_d, inrect.x, inrect.y, inrect.w,
          inrect.h, outrect.x, outrect.y, outrect.w, outrect.h);
    }

    // Increase the ID variable tracking the channels with the number of entries.
    id += (n_entries - 1);

    structure = gst_structure_new_empty ("options");

    gst_structure_set_value (structure,
        GST_CV_VIDEO_CONVERTER_OPT_SRC_RECTANGLES, &srcrects);
    gst_structure_set_value (structure,
        GST_CV_VIDEO_CONVERTER_OPT_DEST_RECTANGLES, &dstrects);

    gst_cv_video_converter_set_input_opts (mlconverter->cvconvert, idx,
        structure);

    g_value_reset (&dstrects);
    g_value_reset (&srcrects);
  }

  g_value_unset (&value);
  g_value_unset (&entry);

  g_value_unset (&dstrects);
  g_value_unset (&srcrects);
}

static GstCaps *
gst_ml_video_converter_translate_ml_caps (GstMLVideoConverter * mlconverter,
    const GstCaps * caps)
{
  GstCaps *result = NULL, *tmplcaps = NULL;
  GstMLInfo mlinfo;
  gint idx = 0, length = 0;

  tmplcaps = gst_caps_new_empty ();
  gst_caps_append_structure (tmplcaps,
      gst_structure_new_empty ("video/x-raw"));

  if (gst_caps_is_empty (caps) || gst_caps_is_any (caps))
    return tmplcaps;

  if (!gst_caps_is_fixed (caps) || !gst_ml_info_from_caps (&mlinfo, caps))
    return tmplcaps;

  result = gst_caps_new_empty ();
  length = gst_caps_get_size (tmplcaps);

  for (idx = 0; idx < length; idx++) {
    GstStructure *structure = gst_caps_get_structure (tmplcaps, idx);
    GstCapsFeatures *features = gst_caps_get_features (tmplcaps, idx);
    // model defaut is NHWC
    guint tensor_batch = mlinfo.tensors[0][0];
    guint tensor_height = mlinfo.tensors[0][1];
    guint tensor_width = mlinfo.tensors[0][2];
    guint tensor_channel = mlinfo.tensors[0][3];

    // tranpose = 2, model is NCHW
    if (mlconverter->transpose == 2) {
      tensor_batch = mlinfo.tensors[0][0];
      tensor_channel = mlinfo.tensors[0][1];
      tensor_height = mlinfo.tensors[0][2];
      tensor_width = mlinfo.tensors[0][3];
    }

    GValue formats = G_VALUE_INIT;
    const GValue *value = NULL;

    // If this is already expressed by the existing caps skip this structure.
    if (idx > 0 && gst_caps_is_subset_structure_full (result, structure, features))
      continue;

    // Make a copy that will be modified.
    structure = gst_structure_copy (structure);

    // 2nd and 3rd dimensions correspond to height and width respectively.
    gst_structure_set (structure,
        "height", G_TYPE_INT, tensor_height,
        "width", G_TYPE_INT, tensor_width,
        NULL);

    // 4th dimension corresponds to the bit depth.
    if (tensor_channel == 1) {
      init_formats (&formats, "GRAY8", NULL);
    } else if (tensor_channel == 3) {
      if (mlconverter->pixlayout == GST_ML_VIDEO_PIXEL_LAYOUT_REGULAR)
        init_formats (&formats, "RGB", NULL);
      else if (mlconverter->pixlayout == GST_ML_VIDEO_PIXEL_LAYOUT_REVERSE)
        init_formats (&formats, "BGR", NULL);
    } else if (tensor_channel == 4) {
      if (mlconverter->pixlayout == GST_ML_VIDEO_PIXEL_LAYOUT_REGULAR)
        init_formats (&formats, "RGBA", "RGBx", "ARGB", "xRGB", NULL);
      else if (mlconverter->pixlayout == GST_ML_VIDEO_PIXEL_LAYOUT_REVERSE)
        init_formats (&formats, "BGRA", "BGRx", "ABGR", "xBGR", NULL);
    }

    gst_structure_set_value (structure, "format", &formats);
    g_value_unset (&formats);

    // Extract the frame rate from ML and propagate it to the video caps.
    value = gst_structure_get_value (gst_caps_get_structure (caps, 0), "rate");

    if (value != NULL)
      gst_structure_set_value (structure, "framerate", value);

    gst_caps_append_structure_full (result, structure,
        gst_caps_features_copy (features));

    // 1st dimension contains the batch size.
    gst_structure_set (structure,
        "batch-size", G_TYPE_INT, tensor_batch,
        NULL);
  }

  gst_caps_unref (tmplcaps);

  GST_DEBUG_OBJECT (mlconverter, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static GstCaps *
gst_ml_video_converter_translate_video_caps (GstMLVideoConverter * mlconverter,
    const GstCaps * caps)
{
  GstCaps *result = NULL;
  GstStructure *structure = NULL;
  GValue dimensions = G_VALUE_INIT, entry = G_VALUE_INIT, dimension = G_VALUE_INIT;
  const GValue *value;

  if (gst_caps_is_empty (caps) || gst_caps_is_any (caps))
    return gst_caps_new_empty_simple ("neural-network/tensors");

  result = gst_caps_new_simple ("neural-network/tensors",
      "type", G_TYPE_STRING, gst_ml_type_to_string (GST_ML_TYPE_UINT8),
      NULL);

  structure = gst_caps_get_structure (caps, 0);

  value = gst_structure_get_value (structure, "width");
  if (NULL == value || !gst_value_is_fixed (value))
    return result;

  value = gst_structure_get_value (structure, "height");
  if (NULL == value || !gst_value_is_fixed (value))
    return result;

  value = gst_structure_get_value (structure, "format");
  if (NULL == value || !gst_value_is_fixed (value))
    return result;

  g_value_init (&dimensions, GST_TYPE_ARRAY);
  g_value_init (&entry, GST_TYPE_ARRAY);
  g_value_init (&dimension, G_TYPE_INT);

  g_value_set_int (&dimension, 1);
  gst_value_array_append_value (&entry, &dimension);

  // 2nd dimension is video height.
  gst_value_array_append_value (&entry,
      gst_structure_get_value (structure, "height"));

  // 3rd dimension is video width.
  gst_value_array_append_value (&entry,
      gst_structure_get_value (structure, "width"));

  value = gst_structure_get_value (structure, "format");

  // 4th dimension is video channels number.
  switch (gst_video_format_from_string (g_value_get_string (value))) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
      g_value_set_int (&dimension, 4);
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      g_value_set_int (&dimension, 3);
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      g_value_set_int (&dimension, 1);
      break;
    default:
      GST_WARNING_OBJECT (mlconverter, "Unsupported format: %s, "
          "falling back to RGB!", g_value_get_string (value));
      g_value_set_int (&dimension, 3);
      break;
  }

  gst_value_array_append_value (&entry, &dimension);
  g_value_unset (&dimension);

  gst_value_array_append_value (&dimensions, &entry);
  g_value_unset (&entry);

  gst_caps_set_value (result, "dimensions", &dimensions);
  g_value_unset (&dimensions);

  // Extract the frame rate from video and propagate it to the ML caps.
  value = gst_structure_get_value (gst_caps_get_structure (caps, 0),
      "framerate");

  if (value != NULL)
    gst_caps_set_value (result, "rate", value);

  GST_DEBUG_OBJECT (mlconverter, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static GstBufferPool *
gst_ml_video_converter_create_pool (GstMLVideoConverter * mlconverter,
    GstCaps * caps)
{
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  GstMLInfo info;

  if (!gst_ml_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (mlconverter, "Invalid caps %" GST_PTR_FORMAT, caps);
    return NULL;
  }

  GST_INFO_OBJECT (mlconverter, "Uses system memory");
  pool = gst_ml_buffer_pool_new (GST_ML_BUFFER_POOL_TYPE_SYSTEM);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, gst_ml_info_size (&info),
      DEFAULT_PROP_MIN_BUFFERS, DEFAULT_PROP_MAX_BUFFERS);

  // name is NULL , get system default allocator
  allocator = gst_allocator_find (NULL);

  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_config_add_option (
      config, GST_ML_BUFFER_POOL_OPTION_TENSOR_META);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_WARNING_OBJECT (mlconverter, "Failed to set pool configuration!");
    g_object_unref (pool);
    pool = NULL;
  }
  g_object_unref (allocator);

  return pool;
}

static gboolean
gst_ml_video_converter_decide_allocation (GstBaseTransform * base,
    GstQuery * query)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);

  GstCaps *caps = NULL;
  GstBufferPool *pool = NULL;
  GstStructure *config = NULL;
  GstAllocator *allocator = NULL;
  guint size, minbuffers, maxbuffers;
  GstAllocationParams params;

  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps) {
    GST_ERROR_OBJECT (mlconverter, "Failed to parse the allocation caps!");
    return FALSE;
  }

  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, NULL, NULL, NULL);

  // Invalidate the cached pool if there is an allocation_query.
  if (mlconverter->outpool)
    gst_object_unref (mlconverter->outpool);

  // Create a new pool in case none was proposed in the query.
  if (!pool && !(pool = gst_ml_video_converter_create_pool (mlconverter, caps))) {
    GST_ERROR_OBJECT (mlconverter, "Failed to create buffer pool!");
    return FALSE;
  }

  mlconverter->outpool = pool;

  // Get the configured pool properties in order to set in query.
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, &caps, &size, &minbuffers,
      &maxbuffers);

  if (gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    gst_query_add_allocation_param (query, allocator, &params);

  gst_structure_free (config);

  // Check whether the query has pool.
  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, minbuffers,
        maxbuffers);
  else
    gst_query_add_allocation_pool (query, pool, size, minbuffers, maxbuffers);

  gst_query_add_allocation_meta (query, GST_ML_TENSOR_META_API_TYPE, NULL);

  return TRUE;
}

static GstFlowReturn
gst_ml_video_converter_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);
  GstBufferPool *pool = mlconverter->outpool;
  guint idx = 0, n_entries = 0;

  if (gst_base_transform_is_passthrough (base)) {
    GST_TRACE_OBJECT (mlconverter, "Passthrough, no need to do anything");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  } else if (gst_base_transform_is_in_place (base)) {
    GST_TRACE_OBJECT (mlconverter, "Inplace, use input buffer as output");
    *outbuffer = inbuffer;
    return GST_FLOW_OK;
  }

  g_return_val_if_fail (pool != NULL, GST_FLOW_ERROR);

  if (!gst_buffer_pool_is_active (pool) &&
      !gst_buffer_pool_set_active (pool, TRUE)) {
    GST_ERROR_OBJECT (mlconverter, "Failed to activate output buffer pool!");
    return GST_FLOW_ERROR;
  }

  // Input is marked as GAP, nothing to process. Create a GAP output buffer.
  if (gst_buffer_get_size (inbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (inbuffer, GST_BUFFER_FLAG_GAP))
    *outbuffer = gst_buffer_new ();

  if ((*outbuffer == NULL) &&
      gst_buffer_pool_acquire_buffer (pool, outbuffer, NULL) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (mlconverter, "Failed to acquire output buffer!");
    return GST_FLOW_ERROR;
  }

  // Copy the flags and timestamps from the input buffer.
  gst_buffer_copy_into (*outbuffer, inbuffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  // Copy the offset field as it may contain channels data for batched buffers.
  GST_BUFFER_OFFSET (*outbuffer) = GST_BUFFER_OFFSET (inbuffer);

  // Set the maximum allowed entries to the size of the tensor batch.
  n_entries = GST_ML_INFO_TENSOR_DIM (mlconverter->mlinfo, 0, 0);

  // Iterate over all possible batch entries and transfer protection meta.
  for (idx = 0; idx < n_entries; idx++) {
    GstProtectionMeta *pmeta = NULL;
    gchar *name = NULL;

    // Check if a bitwise mask was set for this channel/batch input.
    if ((GST_BUFFER_OFFSET (inbuffer) != GST_BUFFER_OFFSET_NONE) &&
        ((GST_BUFFER_OFFSET (inbuffer) & (1 << idx)) == 0))
      continue;

    name = g_strdup_printf ("channel-%u", idx);

    // Copy protection metadata for current memory block into the new buffer.
    if ((pmeta = gst_buffer_get_protection_meta_id (inbuffer, name)) != NULL)
      gst_buffer_add_protection_meta (*outbuffer, gst_structure_copy (pmeta->info));

    g_free (name);
  }

  return GST_FLOW_OK;
}

static GstCaps *
gst_ml_video_converter_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);
  GstCaps *result = NULL, *intersection = NULL;
  const GValue *value = NULL;

  GST_DEBUG_OBJECT (mlconverter, "Transforming caps: %" GST_PTR_FORMAT
      " in direction %s", caps, (direction == GST_PAD_SINK) ? "sink" : "src");
  GST_DEBUG_OBJECT (mlconverter, "Filter caps: %" GST_PTR_FORMAT, filter);


  if (direction == GST_PAD_SINK) {
    GstPad *pad = GST_BASE_TRANSFORM_SRC_PAD (base);
    result = gst_pad_get_pad_template_caps (pad);
  } else if (direction == GST_PAD_SRC) {
    GstPad *pad = GST_BASE_TRANSFORM_SINK_PAD (base);
    result = gst_pad_get_pad_template_caps (pad);
  }

  // Extract the framerate and propagate it to result caps.
  if (!gst_caps_is_empty (caps))
    value = gst_structure_get_value (gst_caps_get_structure (caps, 0),
        (direction == GST_PAD_SRC) ? "rate" : "framerate");

  if (value != NULL) {
    gint idx = 0, length = 0;

    result = gst_caps_make_writable (result);
    length = gst_caps_get_size (result);

    for (idx = 0; idx < length; idx++) {
      GstStructure *structure = gst_caps_get_structure (result, idx);
      gst_structure_set_value (structure,
          (direction == GST_PAD_SRC) ? "framerate" : "rate", value);
    }
  }

  if (filter != NULL) {
    intersection =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (mlconverter, "Returning caps: %" GST_PTR_FORMAT, result);
  return result;
}

static GstCaps *
gst_ml_video_converter_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * incaps, GstCaps * outcaps)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);
  GstCaps *mlcaps = NULL;
  const GValue *value = NULL;

  GST_DEBUG_OBJECT (mlconverter, "Trying to fixate output caps %"
      GST_PTR_FORMAT " based on caps %" GST_PTR_FORMAT " in direction %s",
      outcaps, incaps, (direction == GST_PAD_SINK) ? "sink" : "src");

  // Truncate and make the output caps writable.
  outcaps = gst_caps_truncate (outcaps);
  outcaps = gst_caps_make_writable (outcaps);

  mlcaps = gst_ml_video_converter_translate_video_caps (mlconverter, incaps);

  value = gst_structure_get_value (
    gst_caps_get_structure (outcaps, 0), "dimensions");

  if (NULL == value || !gst_value_is_fixed (value)) {
    value = gst_structure_get_value (
        gst_caps_get_structure (mlcaps, 0), "dimensions");
    gst_caps_set_value (outcaps, "dimensions", value);
  }

  value = gst_structure_get_value (
      gst_caps_get_structure (outcaps, 0), "type");

  if (NULL == value || !gst_value_is_fixed (value)) {
    value = gst_structure_get_value (
        gst_caps_get_structure (mlcaps, 0), "type");
    gst_caps_set_value (outcaps, "type", value);
  }

  gst_caps_unref (mlcaps);
  outcaps = gst_caps_fixate (outcaps);

  GST_DEBUG_OBJECT (mlconverter, "Fixated caps: %" GST_PTR_FORMAT, outcaps);
  return outcaps;
}

static gboolean
gst_ml_video_converter_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);
  GstCaps *othercaps = NULL;
  GstStructure *opts = NULL;
  GstVideoInfo ininfo, outinfo;
  GstMLInfo mlinfo;
  guint bpp = 0, padding = 0;
  gboolean passthrough = FALSE;

  if (!gst_video_info_from_caps (&ininfo, incaps)) {
    GST_ERROR_OBJECT (mlconverter, "Failed to get input video info from caps %"
        GST_PTR_FORMAT "!", incaps);
    return FALSE;
  }

  if (!gst_ml_info_from_caps (&mlinfo, outcaps)) {
    GST_ERROR_OBJECT (mlconverter, "Failed to get output ML info from caps"
        " %" GST_PTR_FORMAT "!", outcaps);
    return FALSE;
  }

  othercaps = gst_ml_video_converter_translate_ml_caps (mlconverter, outcaps);
  othercaps = gst_caps_fixate (othercaps);

  if (!gst_video_info_from_caps (&outinfo, othercaps)) {
    GST_ERROR_OBJECT (mlconverter, "Failed to get output video info from caps %"
        GST_PTR_FORMAT "!", othercaps);
    gst_caps_unref (othercaps);
    return FALSE;
  }

  gst_caps_unref (othercaps);

  // Retrieve the Bits Per Pixel in order to calculate the line padding.
  bpp = GST_VIDEO_FORMAT_INFO_BITS (outinfo.finfo) *
      GST_VIDEO_FORMAT_INFO_N_COMPONENTS (outinfo.finfo);
  // For padding calculations use the video meta if present.
  padding = GST_VIDEO_INFO_PLANE_STRIDE (&outinfo, 0) -
      (GST_VIDEO_INFO_WIDTH (&outinfo) * bpp / 8);

  // Remove any padding from output video info as tensors require none.
  GST_VIDEO_INFO_PLANE_STRIDE (&outinfo, 0) -= padding;
  // Adjust the  video info size to account the removed padding.
  GST_VIDEO_INFO_SIZE (&outinfo) -= padding * GST_VIDEO_INFO_HEIGHT (&outinfo);
  // Additionally adjust the total size depending on the ML type.
  GST_VIDEO_INFO_SIZE (&outinfo) *= gst_ml_type_get_size (mlinfo.type);
  // Additionally adjust the total size depending on the batch size.
  GST_VIDEO_INFO_SIZE (&outinfo) *= GST_ML_INFO_TENSOR_DIM (&mlinfo, 0, 0);
  // Adjust height with the batch number of the tensor (1st dimension).
  GST_VIDEO_INFO_HEIGHT (&outinfo) *= GST_ML_INFO_TENSOR_DIM (&mlinfo, 0, 0);
#if 0 // optimize later
  passthrough =
      GST_VIDEO_INFO_SIZE (&ininfo) == GST_VIDEO_INFO_SIZE (&outinfo) &&
      GST_VIDEO_INFO_WIDTH (&ininfo) == GST_VIDEO_INFO_WIDTH (&outinfo) &&
      GST_VIDEO_INFO_HEIGHT (&ininfo) == GST_VIDEO_INFO_HEIGHT (&outinfo) &&
      GST_VIDEO_INFO_FORMAT (&ininfo) == GST_VIDEO_INFO_FORMAT (&outinfo);
#endif
  gst_base_transform_set_passthrough (base, passthrough);
  gst_base_transform_set_in_place (base, FALSE);

  if (mlconverter->ininfo != NULL)
    gst_video_info_free (mlconverter->ininfo);
  if (mlconverter->vinfo != NULL)
    gst_video_info_free (mlconverter->vinfo);
  if (mlconverter->mlinfo != NULL)
    gst_ml_info_free (mlconverter->mlinfo);

  mlconverter->ininfo = gst_video_info_copy (&ininfo);
  mlconverter->vinfo = gst_video_info_copy (&outinfo);
  mlconverter->mlinfo = gst_ml_info_copy (&mlinfo);

  opts = gst_structure_new_empty ("options");

  if (mlconverter->cvconvert != NULL)
    gst_cv_video_converter_free (mlconverter->cvconvert);

  mlconverter->cvconvert = gst_cv_video_converter_new ();

  gst_structure_set (opts,
      GST_CV_VIDEO_CONVERTER_OPT_NORMALIZE, G_TYPE_BOOLEAN,
          is_normalization_required (mlconverter->mlinfo),
      GST_CV_VIDEO_CONVERTER_OPT_ROFFSET, G_TYPE_DOUBLE,
          GET_MEAN_VALUE (mlconverter->mean, 0),
      GST_CV_VIDEO_CONVERTER_OPT_GOFFSET, G_TYPE_DOUBLE,
          GET_MEAN_VALUE (mlconverter->mean, 1),
      GST_CV_VIDEO_CONVERTER_OPT_BOFFSET, G_TYPE_DOUBLE,
          GET_MEAN_VALUE (mlconverter->mean, 2),
      GST_CV_VIDEO_CONVERTER_OPT_AOFFSET, G_TYPE_DOUBLE,
          GET_MEAN_VALUE (mlconverter->mean, 3),
      GST_CV_VIDEO_CONVERTER_OPT_RSCALE, G_TYPE_DOUBLE,
          GET_SIGMA_VALUE (mlconverter->sigma, 0),
      GST_CV_VIDEO_CONVERTER_OPT_GSCALE, G_TYPE_DOUBLE,
          GET_SIGMA_VALUE (mlconverter->sigma, 1),
      GST_CV_VIDEO_CONVERTER_OPT_BSCALE, G_TYPE_DOUBLE,
          GET_SIGMA_VALUE (mlconverter->sigma, 2),
      GST_CV_VIDEO_CONVERTER_OPT_ASCALE, G_TYPE_DOUBLE,
          GET_SIGMA_VALUE (mlconverter->sigma, 3),
      GST_CV_VIDEO_CONVERTER_OPT_OUTPUT_WIDTH, G_TYPE_UINT,
          GST_VIDEO_INFO_WIDTH (mlconverter->vinfo),
      GST_CV_VIDEO_CONVERTER_OPT_OUTPUT_HEIGHT, G_TYPE_UINT,
          GST_VIDEO_INFO_HEIGHT (mlconverter->vinfo),
      GST_CV_VIDEO_CONVERTER_OPT_CONVERT_NETWORK_TYPE, G_TYPE_UINT,
          mlinfo_network_neural_data_type_serial(mlconverter->mlinfo),
      GST_CV_VIDEO_CONVERTER_OPT_CONVERT, G_TYPE_BOOLEAN,
          mlconverter->convert_enable,
      GST_CV_VIDEO_CONVERTER_OPT_RESIZE, G_TYPE_BOOLEAN,
          mlconverter->resize_enable,
      GST_CV_VIDEO_CONVERTER_OPT_TRANSPOSE, G_TYPE_UINT,
          mlconverter->transpose,
      NULL);

  // Configure the processing pipeline of the opencv converter.
  gst_cv_video_converter_set_output_opts (mlconverter->cvconvert, opts);

  GST_DEBUG_OBJECT (mlconverter, "Input caps: %" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (mlconverter, "Output caps: %" GST_PTR_FORMAT, outcaps);

  return TRUE;
}

static GstFlowReturn
gst_ml_video_converter_transform (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer * outbuffer)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (base);
  GstVideoFrame *inframes = NULL, outframe;
  guint n_inputs = 0;
  gboolean success = TRUE;

  GstClockTime ts_begin = GST_CLOCK_TIME_NONE, ts_end = GST_CLOCK_TIME_NONE;
  GstClockTimeDiff tsdelta = GST_CLOCK_TIME_NONE;

  GST_TRACE("inbuffer: ts=%" GST_TIME_FORMAT ", end_ts=%" GST_TIME_FORMAT
         " off=%" G_GINT64_FORMAT ", end_off=%" G_GINT64_FORMAT
         " memory=%d",
         GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(inbuffer)),
         GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(inbuffer) + GST_BUFFER_DURATION(inbuffer)),
         GST_BUFFER_OFFSET(inbuffer), GST_BUFFER_OFFSET_END(inbuffer),
         gst_buffer_n_memory (inbuffer));

  GST_TRACE("outbuffer: ts=%" GST_TIME_FORMAT ", end_ts=%" GST_TIME_FORMAT
         " off=%" G_GINT64_FORMAT ", end_off=%" G_GINT64_FORMAT
         " memory=%d",
         GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(outbuffer)),
         GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(outbuffer) + GST_BUFFER_DURATION(outbuffer)),
         GST_BUFFER_OFFSET(outbuffer), GST_BUFFER_OFFSET_END(outbuffer),
         gst_buffer_n_memory (outbuffer));

  // GAP buffer, nothing to do. Propagate output buffer downstream.
  if (gst_buffer_get_size (outbuffer) == 0 &&
      GST_BUFFER_FLAG_IS_SET (outbuffer, GST_BUFFER_FLAG_GAP))
    return GST_FLOW_OK;

  // Set the maximum allowed inputs to the size of the tensor batch.
  n_inputs = GST_ML_INFO_TENSOR_DIM (mlconverter->mlinfo, 0, 0);

  success = gst_map_input_video_frames (&inframes, n_inputs, mlconverter->ininfo,
      inbuffer, GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF);

  if (!success) {
    GST_ERROR_OBJECT (mlconverter, "Failed to create input frames!");
    return GST_FLOW_ERROR;
  }

  success = gst_video_frame_map (&outframe, mlconverter->vinfo, outbuffer,
      GST_MAP_READWRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF);

  if (!success) {
    GST_ERROR_OBJECT (mlconverter, "Failed to map output buffer!");
    gst_unmap_input_video_frames (inframes, n_inputs);
    return GST_FLOW_ERROR;
  }

  // Extract and fill aspect ratio meta in output for tensor decryption.
  // Also update the source and destination rectangles of the engine.
  gst_ml_video_converter_update_params (mlconverter, inframes, n_inputs, &outframe);

  ts_begin = gst_util_get_timestamp ();

  if ((n_inputs > 1) || is_conversion_required (&inframes[0], &outframe) ||
      is_normalization_required (mlconverter->mlinfo)) {
    success = gst_cv_video_converter_process (mlconverter->cvconvert,
        inframes, n_inputs, &outframe, 1);
  }

  ts_end = gst_util_get_timestamp ();

  tsdelta = GST_CLOCK_DIFF (ts_begin, ts_end);

  gst_video_frame_unmap (&outframe);
  gst_unmap_input_video_frames (inframes, n_inputs);

  if (!success) {
    GST_ERROR_OBJECT (mlconverter, "Failed to process buffers!");
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (mlconverter, "Conversion took %" G_GINT64_FORMAT ".%03"
      G_GINT64_FORMAT " ms", GST_TIME_AS_MSECONDS (tsdelta),
      (GST_TIME_AS_USECONDS (tsdelta) % 1000));

  return GST_FLOW_OK;
}

static void
gst_ml_video_converter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (object);

  switch (prop_id) {
    case PROP_SUBPIXEL_LAYOUT:
      mlconverter->pixlayout = g_value_get_enum (value);
      break;
    case PROP_MEAN:
    {
      guint idx = 0;

      for (idx = 0; idx < gst_value_array_get_size (value); idx++) {
        gdouble val = g_value_get_double (gst_value_array_get_value (value, idx));
        g_array_append_val (mlconverter->mean, val);
      }
      break;
    }
    case PROP_SIGMA:
    {
      guint idx = 0;

      for (idx = 0; idx < gst_value_array_get_size (value); idx++) {
        gdouble val = g_value_get_double (gst_value_array_get_value (value, idx));
        g_array_append_val (mlconverter->sigma, val);
      }
      break;
    }
    case PROP_CONVERT:
    {
      mlconverter->convert_enable = g_value_get_boolean(value);
      break;
    }
    case PROP_RESIZE:
    {
      mlconverter->resize_enable = g_value_get_boolean(value);
      break;
    }
    case PROP_TRANSPOSE:
    {
      mlconverter->transpose = g_value_get_enum(value);
      break;
    }
    case PROP_ASPECT_RATION:
    {
      mlconverter->aspect_ration = g_value_get_boolean(value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_video_converter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (object);

  switch (prop_id) {
    case PROP_SUBPIXEL_LAYOUT:
      g_value_set_enum (value, mlconverter->pixlayout);
      break;
    case PROP_MEAN:
    {
      GValue val = G_VALUE_INIT;
      guint idx = 0;

      g_value_init (&val, G_TYPE_DOUBLE);

      for (idx = 0; idx < mlconverter->mean->len; idx++) {
        g_value_set_double (&val,
            g_array_index (mlconverter->mean, gdouble, idx));
        gst_value_array_append_value (value, &val);
      }

      g_value_unset (&val);
      break;
    }
    case PROP_SIGMA:
    {
      GValue val = G_VALUE_INIT;
      guint idx = 0;

      g_value_init (&val, G_TYPE_DOUBLE);

      for (idx = 0; idx < mlconverter->sigma->len; idx++) {
        g_value_set_double (&val,
            g_array_index (mlconverter->sigma, gdouble, idx));
        gst_value_array_append_value (value, &val);
      }

      g_value_unset (&val);
      break;
    }
    case PROP_CONVERT:
    {
      g_value_set_boolean (value, mlconverter->convert_enable);
      break;
    }
    case PROP_RESIZE:
    {
      g_value_set_boolean (value, mlconverter->resize_enable);
      break;
    }
    case PROP_TRANSPOSE:
    {
      g_value_set_enum (value, mlconverter->transpose);
      break;
    }
    case PROP_ASPECT_RATION:
    {
      g_value_set_boolean (value, mlconverter->aspect_ration);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ml_video_converter_finalize (GObject * object)
{
  GstMLVideoConverter *mlconverter = GST_ML_VIDEO_CONVERTER (object);

  if (mlconverter->sigma != NULL)
    g_array_free (mlconverter->sigma, TRUE);

  if (mlconverter->mean != NULL)
    g_array_free (mlconverter->mean, TRUE);

  if (mlconverter->cvconvert != NULL)
    gst_cv_video_converter_free (mlconverter->cvconvert);

  if (mlconverter->mlinfo != NULL)
    gst_ml_info_free (mlconverter->mlinfo);

  if (mlconverter->vinfo != NULL)
    gst_video_info_free (mlconverter->vinfo);

  if (mlconverter->ininfo != NULL)
    gst_video_info_free (mlconverter->ininfo);

  if (mlconverter->outpool != NULL)
    gst_object_unref (mlconverter->outpool);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (mlconverter));
}

static void
gst_ml_video_converter_class_init (GstMLVideoConverterClass * klass)
{
  GObjectClass *gobject       = G_OBJECT_CLASS (klass);
  GstElementClass *element    = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base = GST_BASE_TRANSFORM_CLASS (klass);

  gobject->set_property =
      GST_DEBUG_FUNCPTR (gst_ml_video_converter_set_property);
  gobject->get_property =
      GST_DEBUG_FUNCPTR (gst_ml_video_converter_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_video_converter_finalize);

  g_object_class_install_property (gobject, PROP_SUBPIXEL_LAYOUT,
      g_param_spec_enum ("subpixel-layout", "Subpixel Layout",
          "Arrangement of the image pixels in the output tensor",
          GST_TYPE_ML_VIDEO_PIXEL_LAYOUT, DEFAULT_PROP_SUBPIXEL_LAYOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_MEAN,
     gst_param_spec_array ("mean", "Mean Subtraction",
          "Channels mean subtraction values for FLOAT tensors "
          "('<R, G, B>', '<R, G, B, A>', '<G>')",
          g_param_spec_double ("value", "Mean Value",
              "One of B, G or R value.", 0.0, 255.0, DEFAULT_PROP_MEAN,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_SIGMA,
     gst_param_spec_array ("sigma", "Sigma Values",
          "Channel divisor values for FLOAT tensors "
          "('<R, G, B>', '<R, G, B, A>', '<G>')",
          g_param_spec_double ("value", "Sigma Value",
              "One of B, G or R value.", 0.0, 255.0, DEFAULT_PROP_SIGMA,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_CONVERT,
      g_param_spec_boolean ("convert", "Convert Subtraction",
          "Enable color space convert",DEFAULT_PROP_CONVERT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_RESIZE,
      g_param_spec_boolean ("resize", "Resize Subtraction",
          "Enable pixel size to resize",DEFAULT_PROP_RESIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject, PROP_TRANSPOSE,
      g_param_spec_enum ("transpose", "Transpose Subtraction",
          "Operational pixel array transpose",
          GST_TYPE_ML_VIDEO_TRANSPOSE, DEFAULT_PROP_TRANSPOSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (gobject, PROP_ASPECT_RATION,
      g_param_spec_boolean ("aspect-ration", "aspect-ration Subtraction",
          "Enable pixel padding to resize keep aspect buffer",
          DEFAULT_PROP_ASPECTION_RATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element,
      "Machine Learning Video Converter", "Filter/Video/Scaler",
      "Parse an video streams into a ML stream", "QTI");

  gst_element_class_add_pad_template (element,
      gst_ml_video_converter_sink_template ());
  gst_element_class_add_pad_template (element,
      gst_ml_video_converter_src_template ());

  base->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_ml_video_converter_decide_allocation);
  base->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_ml_video_converter_prepare_output_buffer);

  base->transform_caps =
      GST_DEBUG_FUNCPTR (gst_ml_video_converter_transform_caps);
  base->fixate_caps = GST_DEBUG_FUNCPTR (gst_ml_video_converter_fixate_caps);
  base->set_caps = GST_DEBUG_FUNCPTR (gst_ml_video_converter_set_caps);

  base->transform = GST_DEBUG_FUNCPTR (gst_ml_video_converter_transform);
}

static void
gst_ml_video_converter_init (GstMLVideoConverter * mlconverter)
{
  mlconverter->ininfo = NULL;

  mlconverter->vinfo = NULL;
  mlconverter->mlinfo = NULL;

  mlconverter->outpool = NULL;

  mlconverter->cvconvert = NULL;

  mlconverter->pixlayout = DEFAULT_PROP_SUBPIXEL_LAYOUT;
  mlconverter->mean = g_array_new(FALSE, FALSE, sizeof(gdouble));
  mlconverter->sigma = g_array_new(FALSE, FALSE, sizeof(gdouble));
  mlconverter->convert_enable = DEFAULT_PROP_CONVERT;
  mlconverter->resize_enable = DEFAULT_PROP_RESIZE;
  mlconverter->transpose = DEFAULT_PROP_TRANSPOSE;
  mlconverter->aspect_ration = DEFAULT_PROP_ASPECTION_RATION;

  // Handle buffers with GAP flag internally.
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (mlconverter), TRUE);

  GST_DEBUG_CATEGORY_INIT (gst_ml_video_converter_debug, "qtimlvconverter",
      0, "QTI ML video converter plugin");
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtimlvconverter", GST_RANK_NONE,
      GST_TYPE_ML_VIDEO_CONVERTER);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtimlvconverter,
    "QTI Machine Learning plugin for converting video stream into ML stream",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
