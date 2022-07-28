// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstqvdeinpool.h"
#include "qvdeindmabuf.h"

#include <gst/gstinfo.h>
#include <gst/gstmeta.h>
#include <gst/allocators/gstdmabuf.h>

#include <drm/drm_fourcc.h>

GST_DEBUG_CATEGORY_EXTERN (gst_qvdeinterlace_debug);
#define GST_CAT_DEFAULT gst_qvdeinterlace_debug

/* Below is a simple GstQvdeinMeta implementation. */

typedef struct
{
  GstMeta meta;

  const DmaBufDesc *desc;
  //const GstVideoInfo *aligned_info;
} GstQvdeinMeta;

#define GST_QVDEIN_META_API_TYPE (gst_qvdein_meta_api_get_type())
#define GST_QVDEIN_META_INFO     (gst_qvdein_meta_get_info())

#define gst_buffer_get_qvdein_meta(b) \
    ((GstQvdeinMeta *)gst_buffer_get_meta((b),GST_QVDEIN_META_API_TYPE))

static GType
gst_qvdein_meta_api_get_type (void)
{
  static GType type = 0;

  if (g_once_init_enter (&type)) {
    static const gchar *tags[] = { NULL };
    GType _type = gst_meta_api_type_register ("GstQvdeinMetaAPI", tags);
    GST_INFO ("type %" G_GSIZE_FORMAT, (gsize) _type);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static gboolean
gst_qvdein_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstQvdeinMeta *emeta = (GstQvdeinMeta *) meta;

  emeta->desc = NULL;

  return TRUE;
}

static const GstMetaInfo *
gst_qvdein_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *mi = gst_meta_register (GST_QVDEIN_META_API_TYPE,
        "GstQvdeinMeta", sizeof (GstQvdeinMeta),
        gst_qvdein_meta_init, NULL, NULL);
    GST_INFO ("meta info %p", mi);
    g_once_init_leave (&meta_info, mi);
  }
  return meta_info;
}

static GstQvdeinMeta *
_add_qvdein_meta (GstBuffer * buffer, const DmaBufDesc * desc)
{
  GstQvdeinMeta *meta;

  GST_DEBUG ("buffer %p, desc %p", buffer, desc);

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail (desc != NULL, NULL);

  meta = (GstQvdeinMeta *) gst_buffer_add_meta (buffer,
      GST_QVDEIN_META_INFO, NULL);
  g_return_val_if_fail (meta != NULL, NULL);

  meta->desc = desc;

  return meta;
}

/* Below is GstQvdeinPool implementation. */

#define gst_qvdein_pool_parent_class parent_class
G_DEFINE_TYPE (GstQvdeinPool, gst_qvdein_pool, GST_TYPE_BUFFER_POOL);

#if 0
static const gchar **
gst_qvdein_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL };
  return options;
}
#endif

static gboolean
gst_qvdein_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstQvdeinPool *self = GST_QVDEIN_POOL (pool);
  GstVideoInfo info;
  GstCaps *caps;
  guint size, min, max;
  GstAllocator *allocator;
  GstAllocationParams params;

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min, &max)) {
    GST_ERROR_OBJECT (self, "params error");
    return FALSE;
  }

  g_return_val_if_fail (caps != NULL, FALSE);

  GST_INFO_OBJECT (self, "size %u, caps: %" GST_PTR_FORMAT, size, caps);

  if (gst_video_info_from_caps (&info, caps)) {
    g_return_val_if_fail (size >= info.size, FALSE);
    info.size = MAX (size, info.size);
    self->info = info;
  } else {
    GST_ERROR_OBJECT (self, "caps error");
    return FALSE;
  }

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params)) {
    GST_ERROR_OBJECT (self, "allocator error");
    return FALSE;
  }

  GST_INFO_OBJECT (self, "pool %p, allocator %p, size %u, min %u, max %u",
      pool, allocator, (guint) info.size, min, max);

  self->params = params;
  if (self->allocator)
    gst_object_unref (self->allocator);
  if ((self->allocator = allocator))
    gst_object_ref (allocator);

#if 0                           // always add video meta
  /* enable metadata based on config of the pool */
  priv->add_videometa =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);
#endif

  gst_buffer_pool_config_set_params (config, caps, info.size, min, max);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);
}

static G_DEFINE_QUARK (FBufModifierQuark, gst_fbuf_modifier_qdata);

static void
_modifier_free (gpointer modifier)
{
  GST_DEBUG ("modifier %p", modifier);

  if (modifier)
    g_slice_free (guint64, modifier);
}

static inline void
_modifier_attach (GstBuffer * buffer, const DmaBufDesc * desc)
{
  GstMiniObject *mobject = GST_MINI_OBJECT_CAST (buffer);
  guint64 *modifier = g_slice_new (guint64);

  if (!modifier) {
    GST_ERROR ("new modifier error");
    return;
  }

  *modifier = qvdein_dmabuf_get_modifier (desc);
  gst_mini_object_set_qdata (mobject, gst_fbuf_modifier_qdata_quark (),
      modifier, _modifier_free);

  GST_DEBUG ("modifier %p, value 0x%lx, gstbuf %p",
      modifier, *modifier, buffer);
}

/*
static gboolean
gst_is_qvdein_pool_buffer(const GstBufferPool * pool, const GstBuffer * buffer)
{
  gboolean ret = FALSE;
  const gchar *str = "no";

  if (pool == buffer->pool) {
    ret = TRUE;
    str = "yes";
  }

  GST_DEBUG_OBJECT (pool, "buffer %p from pool %p: %s", buffer, pool, str);

  return ret;
}*/

/* The buffer may be allocated by self pool or peer pool. */
gint
gst_qvdein_pool_buffer_get_fd (const GstBufferPool * pool,
    const GstBuffer * buffer)
{
  GstMemory *mem;
  gint fd = -1;

  if (!buffer) {
    GST_ERROR_OBJECT (pool, "pool %p, buffer %p", pool, buffer);
    goto out;
  }

  mem = gst_buffer_peek_memory ((GstBuffer *) buffer, 0);
  fd = gst_dmabuf_memory_get_fd (mem);

out:
  GST_DEBUG_OBJECT (pool, "pool %p, buffer %p, fd %d", pool, buffer, fd);
  return fd;
}

/* The buffer may be allocated by self pool or peer pool. */
gboolean
gst_qvdein_pool_buffer_get_ubwc (const GstBufferPool * pool,
    const GstBuffer * buffer)
{
  guint64 *modifier = NULL;
  gboolean ubwc = FALSE;

  if (!buffer) {
    GST_ERROR_OBJECT (pool, "pool %p, buffer %p", pool, buffer);
    goto out;
  }

  modifier = gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (buffer),
      gst_fbuf_modifier_qdata_quark ());

  if (modifier) {
    ubwc = ((*modifier & DRM_FORMAT_MOD_QCOM_COMPRESSED) ==
        DRM_FORMAT_MOD_QCOM_COMPRESSED);
    GST_DEBUG_OBJECT (pool, "modifier value %" G_GUINT64_FORMAT, *modifier);
  }

out:
  GST_DEBUG_OBJECT (pool, "modifier=%p, ubwc=%u", modifier, ubwc);
  return ubwc;
}

static inline gboolean
do_dmabuf_alloc (GstAllocator * allocator, GstBuffer * buffer,
    DmaBufDesc ** desc, const GstVideoInfo * info, gboolean ubwc)
{
  gboolean ret = FALSE;
  GstMemory *mem;
  gint fd = -1;
  gsize size = 0, info_size = 0;

  if (!qvdein_dmabuf_alloc (desc, info, ubwc)) {
    GST_ERROR_OBJECT (allocator, "alloc error");
    goto out;
  }

  fd = qvdein_dmabuf_get_fd (*desc);
  size = qvdein_dmabuf_get_size (*desc);
  info_size = GST_VIDEO_INFO_SIZE (info);
  /* Have to pass in video info size for allocation to fill GST buffer size.
   * Otherwise, gstbufferpool.c:default_release_buffer() would find size is
   * not equal to the pool saved size set during _decide_allocation(), then
   * discard and free the buffer. */
  mem = gst_dmabuf_allocator_alloc_with_flags (allocator, fd, info_size,
      GST_FD_MEMORY_FLAG_DONT_CLOSE | GST_FD_MEMORY_FLAG_KEEP_MAPPED);
  if (mem) {
    GST_DEBUG_OBJECT (allocator, "dmabuf mem %p", mem);
    gst_buffer_append_memory (buffer, mem);
    ret = TRUE;
  } else {
    GST_ERROR_OBJECT (allocator, "dmabuf mem error");
    qvdein_dmabuf_free (*desc);
  }

out:
  GST_DEBUG_OBJECT (allocator, "info size %" G_GSIZE_FORMAT
      ", allocated fd %d size %" G_GSIZE_FORMAT, info_size, fd, size);

  return ret;
}

static GstFlowReturn
gst_qvdein_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstQvdeinPool *self = GST_QVDEIN_POOL (pool);
  GstVideoInfo *info = &self->info;
  GstVideoInfo *ainfo = &self->aligned_info;
  GstFlowReturn ret = GST_FLOW_ERROR;
  DmaBufDesc *desc = NULL;

  GST_DEBUG_OBJECT (self, "size %" G_GSIZE_FORMAT, info->size);

  *buffer = gst_buffer_new ();
  if (*buffer == NULL) {
    GST_ERROR_OBJECT (self, "buffer new error");
    goto out;
  }

  if (!do_dmabuf_alloc (self->allocator, *buffer, &desc, info, self->ubwc)) {
    GST_ERROR_OBJECT (self, "dmabuf alloc error");
    gst_buffer_unref (*buffer);
    *buffer = NULL;
    goto out;
  }

  (*buffer)->pool = pool;

  if (!self->done_align_info) {
    *ainfo = *info;
    qvdein_dmabuf_align_info (desc, ainfo);
    self->done_align_info = TRUE;
  }

  GST_DEBUG_OBJECT (self, "add GstVideoMeta");
  /* always add video meta */
  gst_buffer_add_video_meta_full (*buffer, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_INFO_FORMAT (ainfo),
      GST_VIDEO_INFO_WIDTH (ainfo), GST_VIDEO_INFO_HEIGHT (ainfo),
      GST_VIDEO_INFO_N_PLANES (ainfo), ainfo->offset, ainfo->stride);

  _add_qvdein_meta (*buffer, desc);
  _modifier_attach (*buffer, desc);

  ret = GST_FLOW_OK;

out:
  GST_DEBUG_OBJECT (self, "buffer %p, params %p", *buffer, params);

  return ret;
}

static inline gboolean
do_dmabuf_free (GstBuffer * buffer)
{
  GstMemory *mem = gst_buffer_peek_memory (buffer, 0);
  gint mem_fd = gst_dmabuf_memory_get_fd (mem);
  GstQvdeinMeta *meta = gst_buffer_get_qvdein_meta (buffer);
  DmaBufDesc *desc;
  gint desc_fd;

  GST_DEBUG ("buffer %p, fd %d", buffer, mem_fd);

  g_return_val_if_fail (meta != NULL, FALSE);

  desc = (DmaBufDesc *) meta->desc;
  g_return_val_if_fail (desc != NULL, FALSE);

  desc_fd = qvdein_dmabuf_get_fd (desc);
  GST_DEBUG ("desc %p, fd %d", desc, desc_fd);

  g_return_val_if_fail (mem_fd == desc_fd, FALSE);

  qvdein_dmabuf_free (desc);

  return TRUE;
}

static void
gst_qvdein_pool_free (GstBufferPool * pool, GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (pool, "buffer %p", buffer);

  do_dmabuf_free (buffer);

  gst_buffer_unref (buffer);
}

GstBufferPool *
gst_qvdein_pool_new (gboolean ubwc)
{
  GstQvdeinPool *pool;

  pool = g_object_new (GST_TYPE_QVDEIN_POOL, NULL);
  gst_object_ref_sink (pool);

  pool->ubwc = ubwc;
  GST_INFO_OBJECT (pool, "pool %p, ubwc %u", pool, ubwc);

  return GST_BUFFER_POOL_CAST (pool);
}

static void
gst_qvdein_pool_finalize (GObject * object)
{
  GstQvdeinPool *pool = GST_QVDEIN_POOL (object);

  GST_INFO_OBJECT (pool, "pool %p", pool);

  if (pool->allocator)
    gst_object_unref (pool->allocator);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_qvdein_pool_class_init (GstQvdeinPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *bpool_class = (GstBufferPoolClass *) klass;

  GST_INFO ("klass %p", klass);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_qvdein_pool_finalize);

  //bpool_class->get_options = gst_qvdein_pool_get_options;
  bpool_class->set_config = GST_DEBUG_FUNCPTR (gst_qvdein_pool_set_config);
  bpool_class->alloc_buffer = GST_DEBUG_FUNCPTR (gst_qvdein_pool_alloc);
  bpool_class->free_buffer = GST_DEBUG_FUNCPTR (gst_qvdein_pool_free);
}

static void
gst_qvdein_pool_init (GstQvdeinPool * pool)
{
  GST_INFO_OBJECT (pool, "pool %p", pool);

  gst_video_info_init (&pool->info);
  gst_video_info_init (&pool->aligned_info);
  pool->allocator = NULL;
  gst_allocation_params_init (&pool->params);

  pool->ubwc = FALSE;
  pool->done_align_info = FALSE;
}
