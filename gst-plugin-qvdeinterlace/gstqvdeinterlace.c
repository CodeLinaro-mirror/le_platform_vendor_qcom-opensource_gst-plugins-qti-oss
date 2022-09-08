// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

/**
 * SECTION:element-qvdeinterlace
 * @title: qvdeinterlace
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=xxx.mp4 ! qtdemux ! h264parse !
 * qcodec2h264dec deinterlace=0 ! qvdeinterlace ! waylandsink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstqvdeinterlace.h"
#include "gstqvdeinpool.h"
#include "gpudeinterlace.h"

#include <gst/video/video.h>
#include <gst/allocators/gstdmabuf.h>
#include <linux-msm/vidc/media/msm_media_info.h>

GST_DEBUG_CATEGORY (gst_qvdeinterlace_debug);
#define GST_CAT_DEFAULT gst_qvdeinterlace_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT,
};

#define SINK_FORMATS "{" \
    "NV12 "  /*  8-bit 4:2:0 */ \
    "}"

#define SRC_FORMATS "{" \
    "NV12 "  /*  8-bit 4:2:0 */ \
    "}"

#define QVDEIN_CAPS_DMABUF(formats) \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES \
    (GST_CAPS_FEATURE_MEMORY_DMABUF, formats)

#define QVDEIN_COMPRESSION_CAPS_DMABUF(formats) \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES \
    (GST_CAPS_FEATURE_MEMORY_DMABUF, formats) \
    ",compression=ubwc"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        QVDEIN_CAPS_DMABUF (SINK_FORMATS) ",interlace-mode=progressive;"
        QVDEIN_COMPRESSION_CAPS_DMABUF (SINK_FORMATS)
        ",interlace-mode={interleaved,mixed},"
        "field-order={top-field-first,bottom-field-first};")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
        QVDEIN_CAPS_DMABUF (SRC_FORMATS) ",interlace-mode=progressive;")
    );

#define gst_qvdeinterlace_parent_class parent_class
G_DEFINE_TYPE (GstQvdeinterlace, gst_qvdeinterlace, GST_TYPE_VIDEO_FILTER);

static void
_print_video_info (const GstVideoInfo * info, const char *func, int line)
{
  GstVideoFormat format;
  gint width, height, stride0, stride1;
  gsize offset0, offset1, size;

  g_return_if_fail (info != NULL);

  format = GST_VIDEO_INFO_FORMAT (info);
  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);
  stride0 = GST_VIDEO_INFO_PLANE_STRIDE (info, 0);
  stride1 = GST_VIDEO_INFO_PLANE_STRIDE (info, 1);
  offset0 = GST_VIDEO_INFO_PLANE_OFFSET (info, 0);
  offset1 = GST_VIDEO_INFO_PLANE_OFFSET (info, 1);
  size = GST_VIDEO_INFO_SIZE (info);

  GST_DEBUG ("%s:%d: format=%s-%d,width=%d,height=%d,stride0=%d,stride1=%d"
      ",offset0=%" G_GSIZE_FORMAT ",offset1=%" G_GSIZE_FORMAT,
      func, line, GST_VIDEO_INFO_NAME (info), format, width, height,
      stride0, stride1, offset0, offset1);
  GST_DEBUG ("%s:%d: size=%" G_GSIZE_FORMAT, func, line, size);
}

#undef print_video_info
#define print_video_info(info) _print_video_info (info, __func__, __LINE__)

static void
_print_video_meta (const GstVideoMeta * meta, const char *func, int line)
{
  g_return_if_fail (meta != NULL);

  GST_DEBUG ("%s:%d: format=%d,width=%u,height=%u,stride0=%d,stride1=%d"
      ",offset0=%" G_GSIZE_FORMAT ",offset1=%" G_GSIZE_FORMAT,
      func, line, meta->format, meta->width, meta->height,
      meta->stride[0], meta->stride[1], meta->offset[0], meta->offset[1]);
}

#undef print_video_meta
#define print_video_meta(meta) _print_video_meta (meta, __func__, __LINE__)

/* GObject vmethod implementations */

static void
gst_qvdeinterlace_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQvdeinterlace *self = GST_QVDEINTERLACE (object);

  GST_DEBUG_OBJECT (self, "prop_id %u", prop_id);

  switch (prop_id) {
    case PROP_SILENT:
      self->silent = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qvdeinterlace_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstQvdeinterlace *self = GST_QVDEINTERLACE (object);

  GST_DEBUG_OBJECT (self, "prop_id %u", prop_id);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, self->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstBaseTransform vmethod implementations */

/* given @caps on the src or sink pad (given by @direction),
 * calculate the possible caps on the other pad.
 * refer to gst_base_transform_transform_caps() for design intent.
 *
 * qvdeinterlace's src and sink caps have no relation to each other,
 * so just ignore input caps and return possible caps on the other pad.
 */
static GstCaps *
gst_qvdeinterlace_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstPad *otherpad;
  GstCaps *result;

  if (GST_PAD_SRC == direction)
    otherpad = trans->sinkpad;
  else
    otherpad = trans->srcpad;

  result = gst_pad_get_pad_template_caps (otherpad);

  if (filter) {
    GstCaps *temp;
    temp = gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (result);
    result = temp;
  }

  GST_DEBUG_OBJECT (trans, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, result);

  return result;
}

/* given fixed @caps, fixate @othercaps,
 * this function is called in gst_base_transform_find_transform().
 *
 * qvdeinterlace's in/out width/height/framerate must be same, while
 * in/out format & interlace-mode may be same or not. pass through if
 * in/out caps are all same.
 */
static GstCaps *
gst_qvdeinterlace_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *result;
  GstPad *pad = (GST_PAD_SINK == direction) ? trans->sinkpad : trans->srcpad;

  GST_DEBUG_OBJECT (pad, "fixate othercaps %" GST_PTR_FORMAT, othercaps);
  GST_DEBUG_OBJECT (pad, "   based on caps %" GST_PTR_FORMAT, caps);

  /* caps must be fixed here, it's an error if it's not */
  g_return_val_if_fail (gst_caps_is_fixed (caps), NULL);

  result = gst_caps_intersect (othercaps, caps);
  if (gst_caps_is_empty (result)) {
    gst_caps_unref (result);
    result = othercaps;
    GST_DEBUG_OBJECT (pad, "intersection is empty");
  } else {
    gst_caps_unref (othercaps);
  }

  GST_DEBUG_OBJECT (pad, "result %" GST_PTR_FORMAT, result);

  if (!gst_caps_is_fixed (result)) {
    /* copy width/height/framerate from caps to fixate othercaps */
    GstStructure *s0 = gst_caps_get_structure (caps, 0);
    const GValue *val;

    result = gst_caps_make_writable (result);

    val = gst_structure_get_value (s0, "width");
    if (val)
      gst_caps_set_value (result, "width", val);

    val = gst_structure_get_value (s0, "height");
    if (val)
      gst_caps_set_value (result, "height", val);

    val = gst_structure_get_value (s0, "framerate");
    if (val)
      gst_caps_set_value (result, "framerate", val);

    /* fixate remaining fields */
    result = gst_caps_fixate (result);
    GST_DEBUG_OBJECT (pad, "result %" GST_PTR_FORMAT, result);
  }

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, result)) {
      GST_DEBUG_OBJECT (pad, "caps is subset of result");
      gst_caps_replace (&result, caps);
    }
  }

  GST_DEBUG_OBJECT (pad, "return %" GST_PTR_FORMAT, result);
  return result;
}

/* buffer's meta & size override video info's */
static gboolean
gst_qvdeinterlace_align_info_by_meta (GstVideoInfo * info,
    const GstVideoMeta * meta, gsize size)
{
  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (meta != NULL, FALSE);

  print_video_info (info);
  print_video_meta (meta);
  GST_DEBUG ("buffer size=%" G_GSIZE_FORMAT, size);

  g_return_val_if_fail (meta->format == GST_VIDEO_INFO_FORMAT (info), FALSE);
  g_return_val_if_fail (size >= GST_VIDEO_INFO_SIZE (info), FALSE);

  GST_VIDEO_INFO_WIDTH (info) = meta->width;
  GST_VIDEO_INFO_HEIGHT (info) = meta->height;
  GST_VIDEO_INFO_PLANE_STRIDE (info, 0) = meta->stride[0];
  GST_VIDEO_INFO_PLANE_STRIDE (info, 1) = meta->stride[1];
  GST_VIDEO_INFO_PLANE_OFFSET (info, 0) = meta->offset[0];
  GST_VIDEO_INFO_PLANE_OFFSET (info, 1) = meta->offset[1];

  GST_VIDEO_INFO_SIZE (info) = size;

  return TRUE;
}

static gboolean
gst_qvdeinterlace_align_info (GstQvdeinterlace * self,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM_CAST (self);
  GstVideoMeta *in_meta;
  GstVideoInfo *out_info;
  GstBufferPool *pool;

  if ((in_meta = gst_buffer_get_video_meta (inbuf)) != NULL) {
    gsize size = gst_buffer_get_size (inbuf);
    gst_qvdeinterlace_align_info_by_meta (&self->in_info, in_meta, size);
  }

  pool = gst_base_transform_get_buffer_pool (trans);
  out_info = gst_qvdein_pool_aligned_info (pool);
  gst_object_unref (pool);

  self->out_info = *out_info;
  print_video_info (&self->out_info);

  GST_INFO_OBJECT (self, "pool %p", pool);

  return TRUE;
}

static gboolean
_caps_has_compression_ubwc (const GstCaps * caps)
{
  GstStructure *s = gst_caps_get_structure (caps, 0);
  const gchar *compression = gst_structure_get_string (s, "compression");

  return g_strcmp0 (compression, "ubwc") == 0 ? TRUE : FALSE;
}

/* Calculate valid size of stride*scanlines with alignment padding of
 * planes but without alignment padding of total size, see format detail
 * in msm_media_info.h. The valid size is for filesink to dump, hence can
 * view the dump correctly by setting line stride and plane scanlines in
 * image player tool. */
static gsize
_calc_valid_size (const GstVideoInfo * info)
{
  gsize size = 0;
  gint format = GST_VIDEO_INFO_FORMAT (info);
  gint width = GST_VIDEO_INFO_WIDTH (info);
  gint height = GST_VIDEO_INFO_HEIGHT (info);

  switch (format) {
    case GST_VIDEO_FORMAT_NV12: {
      int vformat = COLOR_FMT_NV12;
      int y_stride = (int) VENUS_Y_STRIDE(vformat, width);
      int uv_stride = (int) VENUS_UV_STRIDE(vformat, width);
      int y_sclines = (int) VENUS_Y_SCANLINES(vformat, height);
      int uv_sclines = (int) VENUS_UV_SCANLINES(vformat, height);
      size = y_stride * y_sclines + uv_stride * uv_sclines;
      GST_DEBUG ("NV12 valid size %" G_GSIZE_FORMAT, size);
      break;
    }
    default:
      GST_ERROR ("NOT support format %s", GST_VIDEO_INFO_NAME (info));
      break;
  }

  return size;
}

/* this function is called in gst_video_filter_set_caps() that overrides
 * gstbasetransform_class->set_caps().
 * if return TRUE, GstVideoFilter's in/out video info will be set ready.
 */
static gboolean
gst_qvdeinterlace_set_info (GstVideoFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info,
    GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstQvdeinterlace *self = GST_QVDEINTERLACE (filter);
  const GstCapsFeatures *features;
  gboolean ret = TRUE;

  GST_INFO_OBJECT (self, " in_info=%p,  incaps: %" GST_PTR_FORMAT,
      in_info, incaps);
  GST_INFO_OBJECT (self, "out_info=%p, outcaps: %" GST_PTR_FORMAT,
      out_info, outcaps);

  /* when 1st frame comes, align in info by video meta
   * and out info by buffer pool */
  self->in_info = *in_info;
  self->out_info = *out_info;
  /* Set valid size for _decide_allocation() to create output buffer pool
   * and allocate gstbuffer with the valid size for filesink to dump. */
  GST_VIDEO_INFO_SIZE (&self->out_info) = _calc_valid_size (out_info);

  features = gst_caps_get_features (incaps, 0);
  self->in_dmabuf = gst_caps_features_contains (features,
      GST_CAPS_FEATURE_MEMORY_DMABUF);

  features = gst_caps_get_features (outcaps, 0);
  self->out_dmabuf = gst_caps_features_contains (features,
      GST_CAPS_FEATURE_MEMORY_DMABUF);

  if (!self->in_dmabuf || !self->out_dmabuf)
    GST_INFO_OBJECT (self, "in_dmabuf=%u, out_dmabuf=%u",
        self->in_dmabuf, self->out_dmabuf);

  self->in_ubwc = _caps_has_compression_ubwc (incaps);
  self->out_ubwc = _caps_has_compression_ubwc (outcaps);
  GST_INFO_OBJECT (self, "in_ubwc=%u, out_ubwc=%u",
      self->in_ubwc, self->out_ubwc);

  return ret;
}

static gboolean
gst_qvdeinterlace_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  GstQvdeinterlace *self = GST_QVDEINTERLACE (trans);
  GstVideoInfo *info = &self->out_info;
  GstBufferPool *pool = NULL;
  GstCaps *outcaps = NULL;
  GstAllocator *allocator;
  GstStructure *config;
  guint min, max, size;
  gboolean update_pool;

  GST_INFO_OBJECT (self, "%" GST_PTR_FORMAT, query);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    GST_INFO_OBJECT (self, "downstream pool %p, size %u, min %u, max %u",
        pool, size, min, max);

    update_pool = TRUE;
  } else {
    GST_INFO_OBJECT (self, "downstream not propose pool");

    size = 0;
    update_pool = FALSE;
  }

  gst_query_parse_allocation (query, &outcaps, NULL);

  GST_INFO_OBJECT (self, "size %u, info size %u", size, (guint) info->size);
  size = MAX (size, info->size);
  min = 2;
  max = 0;

  GST_INFO_OBJECT (self, "size %u, min %u, max %u", size, min, max);

  if (pool)
    gst_object_unref (pool);

  /* always use its own pool at this time */
  pool = gst_qvdein_pool_new (self->out_ubwc);
  if (!pool) {
    GST_ERROR_OBJECT (self, "pool new error");
    return FALSE;
  }
  //self->pool = pool;

  /* only support dmabuf allocator at this time */
  allocator = gst_dmabuf_allocator_new ();
  if (!allocator) {
    GST_ERROR_OBJECT (self, "allocator new error");
    gst_clear_object (&pool);
    return FALSE;
  }

  GST_INFO_OBJECT (self, "qvdein pool %p, allocator %p", pool, allocator);

  config = gst_buffer_pool_get_config (pool);
  // always add video meta in its own pool
  //gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);
  gst_buffer_pool_set_config (pool, config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  /* GstBaseTransform manages buffer pool created by subclass's _decide_allocation(),
   * so no need to set pool active or inactive in subclass implementation.
   * gstbasetransform.c:1657:default_prepare_output_buffer:<qvdeinterlace0> setting pool 0x7f9d600180a0 active */

#if 0
  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
#else
  /* No need to call parent_class decide_allocation? yes for qvconv of cases waylandsink, filesink & omxh264enc */
  return TRUE;
#endif
}

static void
gst_qvdeinterlace_gpudi_fill_desc (GpudiBufDesc * desc,
    const GstVideoInfo * info, const GstBuffer * buffer, GpudiScanMethod scan)
{
  gint fd = gst_qvdein_pool_buffer_get_fd (buffer->pool, buffer);
  gboolean ubwc = gst_qvdein_pool_buffer_get_ubwc (buffer->pool, buffer);

  gpu_deinterlace_fill_desc (desc, info, fd, ubwc, scan);

  GST_LOG ("fd %d, ubwc %u, scan %d", fd, ubwc, scan);
}

static gboolean
gst_qvdeinterlace_gpudi_open (GstQvdeinterlace * self,
    const GpudiBufDesc * dst, const GpudiBufDesc * src)
{
  int gpudi_handle = gpu_deinterlace_open_instance (dst, src);

  GST_INFO_OBJECT (self, "gpudi_handle=%d", gpudi_handle);

  if (gpudi_handle < 0) {
    GST_ERROR_OBJECT (self, "gpu_deinterlace_open_instance error");
    return FALSE;
  }

  self->gpudi_handle = gpudi_handle;

  self->active = TRUE;

  return TRUE;
}

static void
gst_qvdeinterlace_gpudi_close (GstQvdeinterlace * self)
{
  GST_INFO_OBJECT (self, "gpudi_handle=%d", self->gpudi_handle);

  if (self->gpudi_handle >= 0) {
    gpu_deinterlace_close_instance (self->gpudi_handle);
    self->gpudi_handle = -1;
  }

  self->active = FALSE;
}

static void
gst_qvdeinterlace_reference_buffer_hold (GstQvdeinterlace * self,
    GstBuffer * buffer)
{
  GST_LOG_OBJECT (self, "ref_buf_held %p, buffer %p",
      self->ref_buf_held, buffer);

  if (self->ref_buf_held)
    gst_buffer_unref (self->ref_buf_held);

  self->ref_buf_held = gst_buffer_ref (buffer);
}

static void
gst_qvdeinterlace_reference_buffer_free (GstQvdeinterlace * self)
{
  GST_LOG_OBJECT (self, "ref_buf_held %p", self->ref_buf_held);

  if (self->ref_buf_held) {
    gst_buffer_unref (self->ref_buf_held);
    self->ref_buf_held = NULL;
  }
}

/* Only cover the interlace mode interleaved & mixed at this time.
 * Return progressive scan method in 2 cases of non-interlaced caps & buffer
 * and interlace mode mixed in that a progressive frame comes (in mode mixed,
 * video stream may have interlaced or progressive frame comes), otherwise,
 * return top or bottom field first scan method. */
static GpudiScanMethod
_get_scan_method (const GstVideoInfo * info, const GstBuffer * buffer)
{
  GstVideoInterlaceMode mode = GST_VIDEO_INFO_INTERLACE_MODE (info);
  GstVideoFieldOrder field_order = GST_VIDEO_INFO_FIELD_ORDER (info);
  GpudiScanMethod scan = GPUDI_SCAN_METHOD_PROGRESSIVE;

  /* Default set top field first. */
  if (mode == GST_VIDEO_INTERLACE_MODE_INTERLEAVED ||
      mode == GST_VIDEO_INTERLACE_MODE_MIXED) {
    scan = GPUDI_SCAN_METHOD_TOP_FIRST;
    if (field_order == GST_VIDEO_FIELD_ORDER_BOTTOM_FIELD_FIRST)
      scan = GPUDI_SCAN_METHOD_BOTTOM_FIRST;

    GST_LOG ("scan %d", scan);
  }

  /* Buffer flag overrides video info in cases of mixed mode and
   * interleaved mode with field order unknown. */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED)) {
    if (GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_FLAG_TFF)) {
      if (scan == GPUDI_SCAN_METHOD_BOTTOM_FIRST)
        GST_WARNING ("caps field order conflicts buffer flag");

      scan = GPUDI_SCAN_METHOD_TOP_FIRST;
      GST_LOG ("top field first");
    } else {
      scan = GPUDI_SCAN_METHOD_BOTTOM_FIRST;
      GST_LOG ("bottom field first");
    }
  } else if (mode == GST_VIDEO_INTERLACE_MODE_MIXED) {
    /* If GST_VIDEO_BUFFER_FLAG_INTERLACED not set in mixed mode, then it's a
     * progressive frame. */
    scan = GPUDI_SCAN_METHOD_PROGRESSIVE;
    GST_LOG ("progressive");
  }

  GST_LOG ("interlace mode %d, field order %d, scan %d",
      mode, field_order, scan);

  return scan;
}

static inline gboolean
_check_invalidate_reference_buffer (const GstBuffer * buffer,
    GpudiScanMethod scan_prev, GpudiScanMethod scan)
{
  gboolean discont = GST_BUFFER_IS_DISCONT (buffer);

  GST_LOG ("buffer discont %u, prev scan %d, scan %d",
      discont, scan_prev, scan);

  if (G_UNLIKELY (scan_prev == GPUDI_SCAN_METHOD_NONE))
    return FALSE;

  /* In case of scan method change. */
  if (scan != scan_prev)
    return TRUE;

  /* In cases of seeking or buffer dropping. */
  if (discont && scan_prev != GPUDI_SCAN_METHOD_NONE)
    return TRUE;

  /* In case of progressive frame. */
  if (G_UNLIKELY (scan == GPUDI_SCAN_METHOD_PROGRESSIVE))
    return TRUE;

  return FALSE;
}

#ifndef USE_GPU_DEINTERLACE
/* This function is just for testing without GPU deinterlace. */
static inline gboolean
gst_qvdeinterlace_do_frame_copy (GstQvdeinterlace * self,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstVideoFrame src, dst;

  GST_LOG_OBJECT (self, "just copy without format conversion");

  if (!gst_video_frame_map (&src, &self->in_info, inbuf, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "map inbuf error");
    goto invalid_buffer;
  }

  /* Since gst_fd_mem_map() only return address if mapping flags are a subset
   * of the previous flags, here map it as read and write, thereafter, mapping
   * it again as read in filesink shall succeed, otherwise, mapping late may
   * fail if GstMapFlags is not the subset of the previous flags. */
  if (!gst_video_frame_map (&dst, &self->out_info, outbuf,
          GST_MAP_READ | GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "map outbuf error");
    gst_video_frame_unmap (&src);
    goto invalid_buffer;
  }

  /* need to remove format check predicate in gst_video_frame_copy(),
   * RGBx is pushed directly to ximagesink as BGRx for display */
  if (!gst_video_frame_copy (&dst, &src))
    GST_ERROR_OBJECT (self, "copy buffer error");

  gst_video_frame_unmap (&dst);
  gst_video_frame_unmap (&src);

  return TRUE;

invalid_buffer:
  return FALSE;
}
#endif /* USE_GPU_DEINTERLACE */

/* For GPU HW acceleration, both inbuf & outbuf MUST be DMABUF */
static gboolean
gst_qvdeinterlace_do_transform (GstQvdeinterlace * self,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  //GstVideoFilter *filter = GST_VIDEO_FILTER_CAST (self);
  GpudiBufDesc in_desc, out_desc;
  static GpudiScanMethod in_scan_prev = GPUDI_SCAN_METHOD_NONE;
  GpudiScanMethod in_scan;
  gboolean invalidate;
  gint options = 0;

  in_scan = _get_scan_method (&self->in_info, inbuf);
  invalidate = _check_invalidate_reference_buffer (inbuf,
      in_scan_prev, in_scan);

  GST_LOG_OBJECT (self, "prev scan %d, scan %d, invalidate %u",
      in_scan_prev, in_scan, invalidate);

  in_scan_prev = in_scan;

  if (!self->active) {
    gst_qvdeinterlace_align_info (self, inbuf, outbuf);

    gst_qvdeinterlace_gpudi_fill_desc (&in_desc, &self->in_info, inbuf,
        in_scan);
    gst_qvdeinterlace_gpudi_fill_desc (&out_desc, &self->out_info, outbuf,
        GPUDI_SCAN_METHOD_PROGRESSIVE);

    if (!gst_qvdeinterlace_gpudi_open (self, &out_desc, &in_desc))
      return FALSE;
  } else {
    gst_qvdeinterlace_gpudi_fill_desc (&in_desc, &self->in_info, inbuf,
        in_scan);
    gst_qvdeinterlace_gpudi_fill_desc (&out_desc, &self->out_info, outbuf,
        GPUDI_SCAN_METHOD_PROGRESSIVE);
  }

  options |= gpu_deinterlace_invalidate_reference_option (invalidate);
  GST_LOG_OBJECT (self, "gpu deinterlace options 0x%x", options);

  if (gpu_deinterlace_process_frame (self->gpudi_handle,
          &out_desc, &in_desc, options)) {
    GST_ERROR_OBJECT (self, "gpu_deinterlace_process_frame error");
    return FALSE;
  } else {
    gst_qvdeinterlace_reference_buffer_hold (self, inbuf);
  }

#ifdef USE_GPU_DEINTERLACE
  return TRUE;
#else
  return gst_qvdeinterlace_do_frame_copy (self, inbuf, outbuf);
#endif
}

/* this function is called in default_generate_output() by
 * gst_base_transform_chain() in case of non-passthrough.
 */
static GstFlowReturn
gst_qvdeinterlace_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstQvdeinterlace *self = GST_QVDEINTERLACE (trans);

  if (self->silent == FALSE)
    GST_LOG_OBJECT (self, "inbuf=%p, outbuf=%p", inbuf, outbuf);

  if (!gst_qvdeinterlace_do_transform (self, inbuf, outbuf))
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

/* Override GstBaseTransformClass's default_copy_metadata() not to copy
 * buffer flags like interlaced flags. Instead, qvdeinterlace should set
 * buffer flags itself to reflect the reality. */
static gboolean
gst_qvdeinterlace_copy_metadata (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstQvdeinterlace *self = GST_QVDEINTERLACE (trans);

  GST_LOG_OBJECT (self, "copy timestamps");

  /* when we get here, the outbuf should be writable */
  GST_BUFFER_PTS (outbuf) = GST_BUFFER_PTS (inbuf);
  GST_BUFFER_DTS (outbuf) = GST_BUFFER_DTS (inbuf);
  GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET (inbuf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);
  GST_BUFFER_OFFSET_END (outbuf) = GST_BUFFER_OFFSET_END (inbuf);

  //gst_buffer_copy_into (outbuf, inbuf, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return TRUE;
}

static gboolean
gst_qvdeinterlace_stop (GstBaseTransform * trans)
{
  GstQvdeinterlace *self = GST_QVDEINTERLACE (trans);

  /* gstbasetransform manages lifecycle of buffer pool totally */

  gst_qvdeinterlace_gpudi_close (self);
  gst_qvdeinterlace_reference_buffer_free (self);

  GST_DEBUG_OBJECT (self, "done");

  return TRUE;
}

static void
gst_qvdeinterlace_finalize (GObject * obj)
{
  GstQvdeinterlace *self = GST_QVDEINTERLACE (obj);

  GST_INFO_OBJECT (self, "done");
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/* initialize the qvdeinterlace's class */
static void
gst_qvdeinterlace_class_init (GstQvdeinterlaceClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;
  GstVideoFilterClass *filter_class = (GstVideoFilterClass *) klass;

  GST_INFO ("start");

  gobject_class->set_property = gst_qvdeinterlace_set_property;
  gobject_class->get_property = gst_qvdeinterlace_get_property;
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_qvdeinterlace_finalize);

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
          FALSE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  gst_element_class_set_static_metadata (gstelement_class,
      "QTI Video Deinterlacer",
      "Deinterlacer/Video",
      "Deinterlace interlaced video frame to progressive frame", "QTI");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_qvdeinterlace_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_qvdeinterlace_fixate_caps);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_qvdeinterlace_decide_allocation);
  trans_class->copy_metadata =
      GST_DEBUG_FUNCPTR (gst_qvdeinterlace_copy_metadata);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_qvdeinterlace_transform);
  trans_class->transform_ip = NULL;
  trans_class->passthrough_on_same_caps = TRUE;
  filter_class->set_info = GST_DEBUG_FUNCPTR (gst_qvdeinterlace_set_info);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_qvdeinterlace_stop);
}

static gboolean
gst_qvdeinterlace_load_libs (void)
{
  extern gboolean qvdein_dmabuf_load_libs_once (void);
  gboolean ret = TRUE;

  if (!qvdein_dmabuf_load_libs_once () ||
      !gpu_deinterlace_load_libs_once ()) {
    GST_ERROR ("failed to load libs");
    ret = FALSE;
  }

  return ret;
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_qvdeinterlace_init (GstQvdeinterlace * self)
{
  if (!gst_qvdeinterlace_load_libs ())
    return;

  gst_video_info_init (&self->in_info);
  gst_video_info_init (&self->out_info);
  self->gpudi_handle = -1;
  self->ref_buf_held = NULL;
  self->active = FALSE;
  self->silent = FALSE;
  self->in_dmabuf = FALSE;
  self->out_dmabuf = FALSE;
  self->in_ubwc = FALSE;
  self->out_ubwc = FALSE;

  GST_INFO_OBJECT (self, "done");
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
qvdeinterlace_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_qvdeinterlace_debug, "qvdeinterlace", 0,
      "qvdeinterlace debug category");

  return gst_element_register (plugin, "qvdeinterlace",
      GST_RANK_SECONDARY, GST_TYPE_QVDEINTERLACE);
}

/* gstreamer looks for this structure to register qvdeinterlace */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qvdeinterlace,
    "QTI video deinterlacer",
    qvdeinterlace_init, PACKAGE_VERSION, GST_LICENSE_UNKNOWN, PACKAGE_NAME, "-")
