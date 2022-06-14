// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "qvdeindmabuf.h"

#include <gst/gstinfo.h>

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <drm/drm_fourcc.h>

#ifdef USE_GBM
#include <gbm.h>
#ifdef QTI_PLATFORM
#include <gbm_priv.h>
#endif
#else /* USE_GBM */
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#endif /* USE_GBM */

struct gbm_buf_desc
{
  gint fd;
  gint meta_fd;                 /* GBM meta fd */
  struct gbm_bo *bo;
  void *data;
  guint64 modifier;
  gsize size;
  gint stride;

  gint format;                  /* GBM format */
  gint width;
  gint height;
  gboolean ubwc;
};

GST_DEBUG_CATEGORY_EXTERN (gst_qvdeinterlace_debug);
#define GST_CAT_DEFAULT gst_qvdeinterlace_debug

static int dev_fd = -1;

#ifdef USE_GBM
#define GBM_RENDER_DEVICE_NAME "/dev/dri/renderD128"
//#define GBM_RENDER_DEVICE_NAME "/dev/dri/card0"
static struct gbm_device *gbm_dev = NULL;
#else
#define LINUX_DMABUF_DEVICE_NAME "/dev/dma_heap/system"
#endif

static gboolean
do_dmabuf_device_open (const char *dev_name)
{
  int fd;

  GST_DEBUG ("dev_name %s", dev_name);

  if (dev_fd != -1) {
    GST_DEBUG ("already opened dev_fd %d", dev_fd);
    return TRUE;
  }

  if ((fd = open (dev_name, O_RDONLY | O_CLOEXEC)) < 0) {
    GST_ERROR ("open %s error %s", dev_name, strerror (errno));
    return FALSE;
  } else {
    dev_fd = fd;
    GST_DEBUG ("dev_fd %d", dev_fd);
  }

  return TRUE;
}

static void
do_dmabuf_device_close (void)
{
  GST_DEBUG ("dev_fd %d", dev_fd);
  if (dev_fd < 0)
    return;

  if (close (dev_fd))
    GST_ERROR ("close error %s", strerror (errno));

  dev_fd = -1;
}

#ifdef USE_GBM
static inline gboolean
gbm_dmabuf_fill_desc (DmaBufDesc * desc,
    const GstVideoInfo * info, gboolean ubwc)
{
  GstVideoFormat format;

  if (!desc || !info)
    return FALSE;

  format = GST_VIDEO_INFO_FORMAT (info);
  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      desc->format = GBM_FORMAT_NV12;
      break;
    case GST_VIDEO_FORMAT_BGRx:
      desc->format = GBM_FORMAT_BGRX8888;
      break;
    case GST_VIDEO_FORMAT_BGRA:
      desc->format = GBM_FORMAT_BGRA8888;
      break;
    default:
      GST_ERROR ("NOT support format %s-%d", GST_VIDEO_INFO_NAME (info),
          format);
      return FALSE;
  }

  desc->width = GST_VIDEO_INFO_WIDTH (info);
  desc->height = GST_VIDEO_INFO_HEIGHT (info);
  desc->size = GST_VIDEO_INFO_SIZE (info);
  desc->ubwc = ubwc;

  return TRUE;
}

static inline gboolean
gbm_dmabuf_open (void)
{
  const char *render_name = GBM_RENDER_DEVICE_NAME;

  if (!do_dmabuf_device_open (render_name)) {
    GST_ERROR ("open device error");
    return FALSE;
  }

  gbm_dev = gbm_create_device (dev_fd);
  GST_DEBUG ("gbm_dev %p", gbm_dev);
  if (NULL == gbm_dev) {
    GST_ERROR ("create gbm device error");
    do_dmabuf_device_close ();
    return FALSE;
  }

  return TRUE;
}

static inline void
gbm_dmabuf_close (void)
{
  GST_DEBUG ("gbm_dev %p", gbm_dev);

  if (gbm_dev) {
    gbm_device_destroy (gbm_dev);
    gbm_dev = NULL;
  }

  do_dmabuf_device_close ();
}

static gboolean
gbm_dmabuf_alloc (DmaBufDesc * desc)
{
  uint32_t flags = GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING;
  struct gbm_bo *bo;
  uint32_t width, height;

  GST_DEBUG ("create gbm bo for format 0x%x, width %d, height %d",
      desc->format, desc->width, desc->height);

  desc->fd = desc->meta_fd = -1;

#ifdef QTI_PLATFORM
  if (desc->ubwc)
    flags |= GBM_BO_USAGE_UBWC_ALIGNED_QTI;
#endif

  bo = gbm_bo_create (gbm_dev, desc->width, desc->height, desc->format, flags);
  if (NULL == bo) {
    GST_ERROR ("gbm alloc error %s-%d", strerror (errno), errno);
    return FALSE;
  }

  desc->bo = bo;
  desc->fd = gbm_bo_get_fd (bo);
  width = gbm_bo_get_width (bo);
  height = gbm_bo_get_height (bo);
  desc->stride = gbm_bo_get_stride (bo);
  desc->modifier = gbm_bo_get_modifier (bo);

  GST_DEBUG ("created gbm bo %p, fd %d, width %u, height %u, "
      "stride %u, modifier %lx",
      bo, desc->fd, width, height, desc->stride, desc->modifier);

#ifdef QTI_PLATFORM
  {
    uint32_t size = 0;

    gbm_perform (GBM_PERFORM_GET_METADATA_ION_FD, bo, &desc->meta_fd);

    gbm_perform (GBM_PERFORM_GET_BO_SIZE, bo, &size);
    if ((gsize) size < desc->size)
      GST_WARNING ("gbm bo size %u should >= requested size", size);

    desc->size = (gsize) size;

    GST_DEBUG ("created gbm bo meta_fd %d, size %u", desc->meta_fd, size);
  }
#endif

  return TRUE;
}

static void
gbm_dmabuf_free (DmaBufDesc * desc)
{
  if (!desc) {
    GST_ERROR ("NULL desc");
    return;
  }

  GST_DEBUG ("free gbm bo %p, fd %d, meta_fd %d",
      desc->bo, desc->fd, desc->meta_fd);

  /* TODO: desc->data not mapped yet */

  if (desc->bo) {
    close (desc->fd);
    gbm_bo_destroy (desc->bo);
    desc->bo = NULL;
    desc->fd = -1;
    desc->meta_fd = -1;
  }
}

#else /* USE_GBM */

static inline gboolean
linux_dmabuf_heap_fill_desc (DmaBufDesc * desc, const GstVideoInfo * info)
{
  if (!desc || !info)
    return FALSE;

  desc->len = (__u64) info->size;

  return TRUE;
}

static inline gboolean
linux_dmabuf_heap_open (void)
{
  const char *heap_name = LINUX_DMABUF_DEVICE_NAME;

  return do_dmabuf_device_open (heap_name);
}

static inline void
linux_dmabuf_heap_close (void)
{
  do_dmabuf_device_close ();
}

/* Linux dmabuf heaps is available from kernel 5.10 */
static gboolean
linux_dmabuf_heap_alloc (DmaBufDesc * desc)
{
  struct dma_heap_allocation_data heap_data = {
    .len = desc->len,
    .fd_flags = O_RDWR | O_CLOEXEC,
  };

  GST_DEBUG ("dev_fd %d, size %llu", dev_fd, desc->len);
  if (dev_fd < 0)
    return FALSE;

  if (ioctl (dev_fd, DMA_HEAP_IOCTL_ALLOC, &heap_data)) {
    GST_ERROR ("alloc error %s", strerror (errno));
    return FALSE;
  }

  GST_DEBUG ("heap fd %d", heap_data.fd);
  *desc = heap_data;

  return TRUE;
}

static void
linux_dmabuf_heap_free (DmaBufDesc * desc)
{
  GST_DEBUG ("desc %p, fd %d", desc, desc ? desc->fd : -1);

  if (!desc || desc->fd < 0)
    return;

  if (close (desc->fd))
    GST_ERROR ("close error %s", strerror (errno));

  desc->fd = -1;
}
#endif /* USE_GBM */

/* fill desc from video info for allocation */
static inline gboolean
_qvdein_dmabuf_fill_desc (DmaBufDesc * desc,
    const GstVideoInfo * info, gboolean ubwc)
{
#ifdef USE_GBM
  return gbm_dmabuf_fill_desc (desc, info, ubwc);
#else
  return linux_dmabuf_heap_fill_desc (desc, info);
#endif
}

/* Better cache performance putting the 2 variables in a same bss segment. */
static gint dmabuf_ref_count;
static GMutex dmabuf_ref_mutex;

static inline gboolean
do_dmabuf_open (void)
{
#ifdef USE_GBM
  return gbm_dmabuf_open ();
#else
  return linux_dmabuf_heap_open ();
#endif
}

static gboolean
_qvdein_dmabuf_open (void)
#if 0
/* open dmabuf dev once and shall be closed when process exits */
{
  static gboolean *dmabuf_opened = NULL;

  if (g_once_init_enter (&dmabuf_opened)) {
    gboolean opened = do_dmabuf_open ();
    GST_DEBUG ("opened %u", opened);
    g_once_init_leave (&dmabuf_opened, &opened);
  }

  return *dmabuf_opened;
}
#else
/* open dmabuf only when ref count is zero. */
{
  gboolean ret = TRUE;

  g_mutex_lock (&dmabuf_ref_mutex);

  if (0 == dmabuf_ref_count)
    ret = do_dmabuf_open ();

  if (ret)
    dmabuf_ref_count++;

  GST_DEBUG ("ref count %d", dmabuf_ref_count);

  g_mutex_unlock (&dmabuf_ref_mutex);

  return ret;
}
#endif

static inline void
do_dmabuf_close (void)
{
#ifdef USE_GBM
  gbm_dmabuf_close ();
#else
  linux_dmabuf_heap_close ();
#endif
}

/* close dmabuf only when ref count gets to zero. */
static void
_qvdein_dmabuf_close (void)
{
  g_mutex_lock (&dmabuf_ref_mutex);

  if (dmabuf_ref_count > 0)
    dmabuf_ref_count--;

  if (0 == dmabuf_ref_count)
    do_dmabuf_close ();

  GST_DEBUG ("ref count %d", dmabuf_ref_count);

  g_mutex_unlock (&dmabuf_ref_mutex);
}

static inline gboolean
_qvdein_dmabuf_alloc (DmaBufDesc * desc)
{
#ifdef USE_GBM
  return gbm_dmabuf_alloc (desc);
#else
  return linux_dmabuf_heap_alloc (desc);
#endif
}

static inline void
_qvdein_dmabuf_free (DmaBufDesc * desc)
{
#ifdef USE_GBM
  gbm_dmabuf_free (desc);
#else
  linux_dmabuf_heap_free (desc);
#endif
}

/* Below are external interfaces. */

gboolean
qvdein_dmabuf_alloc (DmaBufDesc ** desc,
    const GstVideoInfo * info, gboolean ubwc)
{
  GST_DEBUG ("ubwc %u, size %" G_GSIZE_FORMAT,
      ubwc, GST_VIDEO_INFO_SIZE (info));

  *desc = g_new0 (DmaBufDesc, 1);
  if (NULL == *desc) {
    GST_ERROR ("no memory");
    return FALSE;
  }

  if (!_qvdein_dmabuf_fill_desc (*desc, info, ubwc))
    goto desc_free;

  if (!_qvdein_dmabuf_open ()) {
    GST_ERROR ("open error");
    goto desc_free;
  }

  if (!_qvdein_dmabuf_alloc (*desc)) {
    GST_ERROR ("alloc error");
    goto dmabuf_close;
  }

  GST_DEBUG ("desc %p, size %" G_GSIZE_FORMAT,
      *desc, qvdein_dmabuf_get_size (*desc));

  return TRUE;

dmabuf_close:
  _qvdein_dmabuf_close ();

desc_free:
  g_free (*desc);
  *desc = NULL;

  return FALSE;
}

gint
qvdein_dmabuf_get_fd (const DmaBufDesc * desc)
{
  gint fd = -1;

  if (desc)
    fd = (gint) desc->fd;

  GST_DEBUG ("desc %p, fd=%d", desc, fd);

  return fd;
}

gsize
qvdein_dmabuf_get_size (const DmaBufDesc * desc)
{
  gsize size = 0;

  if (desc)
#ifdef USE_GBM
    size = (gsize) desc->size;
#else
    size = (gsize) desc->len;
#endif

  GST_DEBUG ("desc %p, size %" G_GSIZE_FORMAT, desc, size);

  return size;
}

guint64
qvdein_dmabuf_get_modifier (const DmaBufDesc * desc)
{
  uint64_t modifier = DRM_FORMAT_MOD_LINEAR;

  if (desc) {
#ifdef USE_GBM
    if (desc->bo)
      modifier = gbm_bo_get_modifier (desc->bo);
#else
    /* NOT implemented for Linux dmabuf heaps. */
#endif
  }

  GST_DEBUG ("desc %p, modifier 0x%lx", desc, modifier);

  return (guint64) modifier;
}

/* align info by allocated desc */
void
qvdein_dmabuf_align_info (const DmaBufDesc * desc, GstVideoInfo * info)
{
  GST_DEBUG ("desc %p, info=%p", desc, info);
  if (!desc || !info)
    return;

#ifdef USE_GBM
  GST_VIDEO_INFO_PLANE_STRIDE (info, 0) = desc->stride;
  /* FIXME: stride0 == stride1 is true for NV12, may be not true for others */
  GST_VIDEO_INFO_PLANE_STRIDE (info, 1) = desc->stride;
  GST_VIDEO_INFO_PLANE_OFFSET (info, 1) = gbm_bo_get_offset (desc->bo, 1);
  GST_VIDEO_INFO_SIZE (info) = desc->size;

  GST_DEBUG ("aligned info stride %u, offset1 %u, size %lu", desc->stride,
      gbm_bo_get_offset (desc->bo, 1), desc->size);
#endif /* USE_GBM */
}

void
qvdein_dmabuf_free (DmaBufDesc * desc)
{
  GST_DEBUG ("desc %p", desc);

  _qvdein_dmabuf_free (desc);
  g_free (desc);

  _qvdein_dmabuf_close ();
}
