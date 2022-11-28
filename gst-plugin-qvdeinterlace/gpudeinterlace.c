// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gpudeinterlace.h"

#include <gst/gstinfo.h>

#include <dlfcn.h>
#include <drm/drm_fourcc.h>

GST_DEBUG_CATEGORY_EXTERN (gst_qvdeinterlace_debug);
#define GST_CAT_DEFAULT gst_qvdeinterlace_debug

/* Dynamically load libgpudi by dlopen. */
#define ADRENO_GPUDI_LIB_NAME "libgpudi.so.1"

static const char *adreno_gpudi_lib_name  = ADRENO_GPUDI_LIB_NAME;

/* Adreno GPUDI APIs */
static GpuDeinterlaceContext* (*_GpuDeinterlaceInit) ();
static int (*_GpuDeinterlaceGetCapability) (GpuDeinterlace_Caps *caps);
static int (*_GpuDeinterlaceGetInputFormats) (unsigned int *count,
    unsigned int **formats);
static int (*_GpuDeinterlaceGetOutputFormats) (unsigned int *count,
    unsigned int **formats);
static int (*_GpuDeinterlaceBlt) (GpuDeinterlaceContext* context,
    GpuDeinterlace_BufferDesc* output_buf,
    GpuDeinterlace_BufferDesc* input_buf,
    GpuDeinterlace_BltOption option);
static int (*_GpuDeinterlaceReset) (GpuDeinterlaceContext* context);
static int (*_GpuDeinterlaceRelease) (GpuDeinterlaceContext* context);

#define LOAD_SYMBOL(lib, sym) do {                        \
      dlerror (); /* clear any existing error */          \
      *(void **) & (_ ## sym) = dlsym (lib, #sym);        \
      const char *dlerr = dlerror ();                     \
      if (NULL != dlerr) {                                \
        GST_ERROR ("dlsym error: %s", dlerr);             \
        goto error;                                       \
      }                                                   \
      GST_DEBUG ("loaded symbol %s", #sym);               \
    } while (0)

static gpointer _gpudi_load_lib_symbols (gpointer data)
{
  gpointer ret = NULL;
  void *handle_gpudi;

  GST_INFO ("data %p", data);

  handle_gpudi = dlopen (adreno_gpudi_lib_name, RTLD_NOW);
  if (NULL == handle_gpudi) {
    const char *dlerr = dlerror();
    if (NULL == dlerr)
        dlerr = "NULL";
    GST_ERROR ("dlopen %s error: %s", adreno_gpudi_lib_name, dlerr);
    goto error;
  }

  LOAD_SYMBOL (handle_gpudi, GpuDeinterlaceInit);
  LOAD_SYMBOL (handle_gpudi, GpuDeinterlaceGetCapability);
  LOAD_SYMBOL (handle_gpudi, GpuDeinterlaceGetInputFormats);
  LOAD_SYMBOL (handle_gpudi, GpuDeinterlaceGetOutputFormats);
  LOAD_SYMBOL (handle_gpudi, GpuDeinterlaceBlt);
  LOAD_SYMBOL (handle_gpudi, GpuDeinterlaceReset);
  LOAD_SYMBOL (handle_gpudi, GpuDeinterlaceRelease);

  ret = (gpointer) -1; /* load all okay */

error:
  GST_INFO ("ret %p", ret);
  return ret;
}

/* Load libs only once in multi-threaded usage. */
gboolean gpu_deinterlace_load_libs_once (void)
{
  static GOnce once = G_ONCE_INIT;

  g_once (&once, _gpudi_load_lib_symbols, NULL);
  GST_INFO ("GOnce retval %p status %d", once.retval, once.status);

  return once.retval != NULL ? TRUE : FALSE;
}

#define GpuDeinterlaceInit _GpuDeinterlaceInit
#define GpuDeinterlaceGetCapability _GpuDeinterlaceGetCapability
#define GpuDeinterlaceGetInputFormats _GpuDeinterlaceGetInputFormats
#define GpuDeinterlaceGetOutputFormats _GpuDeinterlaceGetOutputFormats
#define GpuDeinterlaceBlt _GpuDeinterlaceBlt
#define GpuDeinterlaceReset _GpuDeinterlaceReset
#define GpuDeinterlaceRelease _GpuDeinterlaceRelease


#ifdef USE_GPU_DEINTERLACE
#define MAX_GPUDI_INSTANCE 2

struct gpudi_instance
{
  void *context;
};

static struct gpudi_instance gpudi_instance[MAX_GPUDI_INSTANCE];
static guint gpudi_instance_number;
static GMutex gpudi_instance_mutex;

static gint
gpudi_instance_handle_alloc (void)
{
  gint handle = -1;
  gint i;

  g_mutex_lock (&gpudi_instance_mutex);

  GST_DEBUG ("instance number %u MUST <= %u",
      gpudi_instance_number + 1, MAX_GPUDI_INSTANCE);

  if (gpudi_instance_number >= MAX_GPUDI_INSTANCE) {
    GST_ERROR ("can NOT allocate more handle");
    goto unlock;
  }

  for (i = 0; i < MAX_GPUDI_INSTANCE; i++) {
    if (gpudi_instance[i].context == NULL) {
      gpudi_instance[i].context = (void *) -1L; /* place holder */
      handle = i;
      break;
    }
  }

  if (i == MAX_GPUDI_INSTANCE) {
    GST_ERROR ("gpudi handle allocation error");
    goto unlock;
  }

  gpudi_instance_number++;

unlock:
  g_mutex_unlock (&gpudi_instance_mutex);

  GST_DEBUG ("handle %d", handle);

  return handle;
}

static void
gpudi_instance_handle_free (gint handle)
{
  g_return_if_fail (handle >= 0 && handle < MAX_GPUDI_INSTANCE);

  g_mutex_lock (&gpudi_instance_mutex);

  g_warn_if_fail (gpudi_instance[handle].context != NULL);

  gpudi_instance[handle].context = NULL;
  gpudi_instance_number--;

  GST_DEBUG ("instance number %u", gpudi_instance_number);

  g_mutex_unlock (&gpudi_instance_mutex);
}

static inline void
gpudi_instance_context_set (gint handle, void *context)
{
  gpudi_instance[handle].context = context;
}

static inline void *
gpudi_instance_context_get (gint handle)
{
  if (handle >= MAX_GPUDI_INSTANCE || handle < 0) {
    GST_ERROR ("invalid handle %d", handle);
    return NULL;
  }

  return gpudi_instance[handle].context;
}

static inline guint32
gpudi_drm_format (GstVideoFormat format)
{
  guint32 drm_format = DRM_FORMAT_INVALID;

  switch (format) {
    case GST_VIDEO_FORMAT_NV12:
      drm_format = DRM_FORMAT_NV12;
      break;
    default:
      GST_ERROR ("NOT support format %s", gst_video_format_to_string (format));
      break;
  };

  return drm_format;
}

static gboolean
gpudi_support_format (const guint32 * formats, guint count, guint32 format)
{
  g_return_val_if_fail (formats != NULL, FALSE);
  g_return_val_if_fail (count > 0, FALSE);

  for (guint i = 0; i < count; i++)
    if (format == formats[i])
      return TRUE;

  return FALSE;
}

static inline gboolean
do_gpudi_support_caps (const GpudiBufDesc * desc,
    const GpuDeinterlace_Caps * caps, gboolean input)
{
  guint count = 0;
  guint32 *formats = NULL;
  const char *direction = input ? "in" : "out";
  int ret;

  g_return_val_if_fail (desc->width > 0, FALSE);
  g_return_val_if_fail (desc->width <= caps->max_width, FALSE);

  g_return_val_if_fail (desc->height > 0, FALSE);
  g_return_val_if_fail (desc->height <= caps->max_height, FALSE);

  if (input)
    ret = GpuDeinterlaceGetInputFormats (&count, &formats);
  else
    ret = GpuDeinterlaceGetOutputFormats (&count, &formats);

  if (ret != 0) {
    GST_ERROR ("get %s formats error, ret %d", direction, ret);
    return FALSE;
  }

  if (!gpudi_support_format (formats, count, desc->format)) {
    GST_ERROR ("not support %s drm format %u", direction, desc->format);
    return FALSE;
  }

  return TRUE;
}
#endif /* USE_GPU_DEINTERLACE */

static inline gboolean
gpudi_support_caps (const GpudiBufDesc * dst, const GpudiBufDesc * src)
{
#ifdef USE_GPU_DEINTERLACE
  GpuDeinterlace_Caps caps = { 0, };
  int ret;

  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (dst != NULL, FALSE);

  if ((ret = GpuDeinterlaceGetCapability (&caps)) != 0) {
    GST_ERROR ("get caps error, ret=%d", ret);
    return FALSE;
  }

  if (!do_gpudi_support_caps (src, &caps, TRUE))
    return FALSE;

  if (!do_gpudi_support_caps (dst, &caps, FALSE))
    return FALSE;
#endif /* USE_GPU_DEINTERLACE */

  return TRUE;
}

static inline gint
do_open_gpudi_instance (void)
{
#ifdef USE_GPU_DEINTERLACE
  gint handle = -1;
  GpuDeinterlaceContext *context;

  if ((context = GpuDeinterlaceInit ()) == NULL) {
    GST_ERROR ("init instance error");
    goto out;
  }

  handle = gpudi_instance_handle_alloc ();
  if (handle < 0)
    goto out;

  gpudi_instance_context_set (handle, context);

out:
  GST_DEBUG ("context %p, handle %d", context, handle);

  return handle;
#else
  return 0;
#endif /* USE_GPU_DEINTERLACE */
}

static inline gint
do_close_gpudi_instance (gint handle)
{
#ifdef USE_GPU_DEINTERLACE
  gint ret = -1;
  GpuDeinterlaceContext *context;

  GST_DEBUG ("handle %d", handle);

  context = gpudi_instance_context_get (handle);
  if (context == NULL)
    goto out;

  ret = GpuDeinterlaceRelease (context);
  if (ret)
    GST_ERROR ("release error, ret %d", ret);

  gpudi_instance_handle_free (handle);

out:
  GST_DEBUG ("context %p, ret %d", context, ret);

  return ret;
#else
  return 0;
#endif /* USE_GPU_DEINTERLACE */
}

void
gpu_deinterlace_fill_desc (GpudiBufDesc * desc, const GstVideoInfo * info,
    gint fd, gboolean ubwc, GpudiScanMethod scan)
{
  GST_DEBUG ("desc %p, info %p, fd %d, ubwc %u, scan %d",
      desc, info, fd, ubwc, scan);

  g_return_if_fail (desc != NULL);
  g_return_if_fail (info != NULL);

#ifdef USE_GPU_DEINTERLACE
  desc->fd = fd;
  desc->format = gpudi_drm_format (GST_VIDEO_INFO_FORMAT (info));
  desc->width = GST_VIDEO_INFO_WIDTH (info);
  desc->height = GST_VIDEO_INFO_HEIGHT (info);
  desc->stride = GST_VIDEO_INFO_PLANE_STRIDE (info, 0);
  desc->compressed = ubwc ? 1 : 0;
  desc->scan = scan;

  GST_DEBUG ("desc fd %d, DRM format %u, width %u, height %u, "
      "stride %u, ubwc %u, scan %d, info size %" G_GSIZE_FORMAT,
      desc->fd, desc->format, desc->width, desc->height,
      desc->stride, desc->compressed, desc->scan, GST_VIDEO_INFO_SIZE (info));
#endif /* USE_GPU_DEINTERLACE */
}

/* return a GPU deinterlace instance handle */
gint
gpu_deinterlace_open_instance (const GpudiBufDesc * dst,
    const GpudiBufDesc * src)
{
  gint handle = -1;

  if (gpudi_support_caps (dst, src))
    handle = do_open_gpudi_instance ();

  GST_DEBUG ("handle %d", handle);

  return handle;
}

/* 2 cases of seeking & field order change need to set flags bit to
 * invalidate reference frame, gpudi does bob for 1st new frame */
gint
gpu_deinterlace_process_frame (gint handle, GpudiBufDesc * dst,
    const GpudiBufDesc * src, gint options)
{
#ifdef USE_GPU_DEINTERLACE
  gint ret = -1;
  GpuDeinterlaceContext *context;

  GST_DEBUG ("handle %d, dst %p, src %p, options 0x%x",
      handle, dst, src, options);

  g_return_val_if_fail (dst != NULL && src != NULL, ret);

  context = gpudi_instance_context_get (handle);
  if (context == NULL)
    goto out;

  ret = GpuDeinterlaceBlt (context, dst, (GpudiBufDesc *) src, options);
  if (ret)
    GST_ERROR ("deinterlace frame error, ret %d", ret);

out:
  GST_DEBUG ("context %p, ret %d", context, ret);

  return ret;
#else
  return 0;
#endif /* USE_GPU_DEINTERLACE */
}

/* in case of resolution change, must do reset by this firstly, and close
 * old dmabuf fd set, then reconfigure and allocate new dmabuf fd set */
gint
gpu_deinterlace_reset_instance (gint handle)
{
#ifdef USE_GPU_DEINTERLACE
  gint ret = -1;
  GpuDeinterlaceContext *context;

  GST_DEBUG ("handle %d", handle);

  context = gpudi_instance_context_get (handle);
  if (context == NULL)
    goto out;

  ret = GpuDeinterlaceReset (context);
  if (ret)
    GST_ERROR ("reset error, ret %d", ret);

out:
  GST_DEBUG ("context %p, ret %d", context, ret);

  return ret;
#else
  return 0;
#endif /* USE_GPU_DEINTERLACE */
}

gint
gpu_deinterlace_close_instance (gint handle)
{
  gint ret = do_close_gpudi_instance (handle);

  GST_DEBUG ("ret %d", ret);

  return ret;
}
