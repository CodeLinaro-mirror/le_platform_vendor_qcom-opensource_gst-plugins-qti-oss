// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

/**
 * SECTION:element-vesdeliver
 * @title: vesdeliver
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=xxx.mp4 ! qtdemux ! h264parse !
 * vesdeliver secure=1 ! qcodec2h264dec secure=1 ! waylandsink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include <dlfcn.h>
#include "gstvesdeliver.h"
#include "gstvesdeliverallocator.h"

/* Dynamically load libs by dlopen. */
static const char *crypto_lib_name  = "libcontentcopy.so";
static const char *crypto_app_name = "smpcpyap64";

GST_DEBUG_CATEGORY (vesdeliver_debug);
#define GST_CAT_DEFAULT vesdeliver_debug

enum {
  PROP_0,
  PROP_SECURE,
};

static GstStaticPadTemplate sink_tmpl = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (H264_CAPS";"H265_CAPS";"VP9_CAPS";"MPEG2_CAPS));

static GstStaticPadTemplate src_tmpl = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (H264_CAPS";"H265_CAPS";"VP9_CAPS";"MPEG2_CAPS));

#define gst_vesdeliver_parent_class parent_class
G_DEFINE_TYPE (GstVesDeliver, gst_vesdeliver, GST_TYPE_BASE_TRANSFORM);

#define DEFAULT_SECURE_MODE    (TRUE)
#define SECURE_COPY_RETURN_SUCCESS      0
#define SECURE_COPY_NONSECURE_TO_SECURE 0

static GstFlowReturn
gst_vesdeliver_prepare_output_buffer (GstBaseTransform * base,
   GstBuffer * inbuffer, GstBuffer ** outbuffer);
static GstFlowReturn
gst_vesdeliver_transform (GstBaseTransform * trans, GstBuffer * inbuffer,
    GstBuffer * outbuffer);
static void
gst_vesdeliver_set_property(GObject* object, guint property_id,
    const GValue* value, GParamSpec* pspec);
static void
gst_vesdeliver_get_property(GObject* object, guint property_id,
    GValue* value, GParamSpec* pspec);
static void
gst_vesdeliver_finalize (GObject * object);
static gboolean
gst_vesdeliver_start (GstBaseTransform* trans);
static gboolean
gst_vesdeliver_stop (GstBaseTransform* trans);

static void
gst_vesdeliver_class_init (GstVesDeliverClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetrans_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasetrans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_vesdeliver_set_property;
  gobject_class->get_property = gst_vesdeliver_get_property;
  gobject_class->finalize = gst_vesdeliver_finalize;

  g_object_class_install_property(
      gobject_class,
      PROP_SECURE,
      g_param_spec_boolean(
          "secure",
          "Secure",
          "video pipeline in secure mode, will allocate secure buffer",
          DEFAULT_SECURE_MODE,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gstbasetrans_class->transform = GST_DEBUG_FUNCPTR (gst_vesdeliver_transform);
  gstbasetrans_class->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_vesdeliver_prepare_output_buffer);
  gstbasetrans_class->start = GST_DEBUG_FUNCPTR (gst_vesdeliver_start);
  gstbasetrans_class->stop = GST_DEBUG_FUNCPTR (gst_vesdeliver_stop);

  gst_element_class_add_static_pad_template (gstelement_class, &sink_tmpl);
  gst_element_class_add_static_pad_template (gstelement_class, &src_tmpl);

  gst_element_class_set_static_metadata (gstelement_class,
      "QTI Video Element Stream Deliver",
      "Deliver/Video",
      "video deliver plugin which supports secure buffer sharing with dec plugin",
      "QTI");
}

static void
gst_vesdeliver_init (GstVesDeliver * vesdeliver)
{
  vesdeliver->secure = TRUE;
  vesdeliver->allocator = NULL;
  vesdeliver->secure_handle = NULL;

  vesdeliver->crypto_handle = dlopen (crypto_lib_name, RTLD_NOW);
  if (NULL == vesdeliver->crypto_handle) {
    const char *dlerr = dlerror();
    if (NULL == dlerr)
        dlerr = "NULL";
    GST_ERROR ("dlopen %s error: %s", crypto_lib_name, dlerr);
    return;
  }

  vesdeliver->Content_Protection_Set_AppName =
      dlsym (vesdeliver->crypto_handle, "Content_Protection_Set_AppName");
  vesdeliver->Content_Protection_Copy_Init =
      dlsym (vesdeliver->crypto_handle, "Content_Protection_Copy_Init");
  vesdeliver->Content_Protection_Copy =
      dlsym (vesdeliver->crypto_handle, "Content_Protection_Copy");
  vesdeliver->Content_Protection_Copy_Terminate =
      dlsym (vesdeliver->crypto_handle, "Content_Protection_Copy_Terminate");

  if (!vesdeliver->Content_Protection_Set_AppName ||
      !vesdeliver->Content_Protection_Copy_Init ||
      !vesdeliver->Content_Protection_Copy ||
      !vesdeliver->Content_Protection_Copy_Terminate) {
    GST_ERROR ("dlsym failed with NULL symbol");
    dlclose (vesdeliver->crypto_handle);
    vesdeliver->crypto_handle = NULL;
    return;
  } else {
    GST_INFO_OBJECT (vesdeliver, "open %s(%p) successfully",
          crypto_lib_name, vesdeliver->crypto_handle);
  }

  int ret = SECURE_COPY_RETURN_SUCCESS;
  ret = vesdeliver->Content_Protection_Copy_Init (&vesdeliver->secure_handle);
  if (ret == SECURE_COPY_RETURN_SUCCESS) {
    ret = vesdeliver->Content_Protection_Set_AppName (crypto_app_name);
    if (ret != SECURE_COPY_RETURN_SUCCESS) {
      GST_ERROR ("Content_Protection_Set_AppName failed with %d", ret);
    }
  } else {
    GST_ERROR ("Content_Protection_Copy_Init failed with %d", ret);
  }

  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (vesdeliver), FALSE);
}

static void
gst_vesdeliver_finalize (GObject * object)
{
  GstVesDeliver *vesdeliver = GST_VESDELIVER (object);

  int ret = SECURE_COPY_RETURN_SUCCESS;
  ret = vesdeliver->Content_Protection_Copy_Terminate (&vesdeliver->secure_handle);
  if (ret != SECURE_COPY_RETURN_SUCCESS) {
    GST_ERROR ("Content_Protection_Copy_Terminate failed with %d", ret);
  }

  if (vesdeliver->crypto_handle) {
    GST_INFO_OBJECT (vesdeliver, "dlclose %s(%p)", crypto_lib_name, vesdeliver->crypto_handle);
    dlclose (vesdeliver->crypto_handle);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_vesdeliver_start (GstBaseTransform* trans)
{
  GstVesDeliver *vesdeliver = GST_VESDELIVER (trans);

  if (NULL == vesdeliver->allocator) {
    vesdeliver->allocator = gst_vesdeliver_allocator_new (vesdeliver->secure);
    g_return_val_if_fail (vesdeliver->allocator != NULL, FALSE);
    GST_DEBUG_OBJECT (vesdeliver, "Create vesdeliver allocator");
  }
  return TRUE;
}

static gboolean
gst_vesdeliver_stop (GstBaseTransform* trans)
{
  GstVesDeliver *vesdeliver = GST_VESDELIVER (trans);

  if (vesdeliver->allocator) {
    gst_object_unref (vesdeliver->allocator);
    vesdeliver->allocator = NULL;
    GST_DEBUG_OBJECT (vesdeliver, "Unref vesdeliver allocator");
  }

  return TRUE;
}

static GstFlowReturn
gst_vesdeliver_prepare_output_buffer (GstBaseTransform * trans,
   GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstVesDeliver *vesdeliver = GST_VESDELIVER (trans);

  *outbuf = gst_buffer_new_allocate (vesdeliver->allocator,
                gst_buffer_get_size(inbuf), NULL);

  g_return_val_if_fail (*outbuf != NULL, GST_FLOW_ERROR);
  GST_BASE_TRANSFORM_CLASS (parent_class)->copy_metadata (trans, inbuf, *outbuf);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vesdeliver_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVesDeliver *vesdeliver = GST_VESDELIVER (trans);
  GstMemory *out_mem = NULL;
  int buf_fd = -1;
  gsize fd_memory_size = 0;

  g_return_val_if_fail (gst_buffer_is_writable (outbuf), GST_FLOW_ERROR);

  GstMapInfo input_map = {};
  gst_buffer_map (inbuf, &input_map, GST_MAP_READ);
  GST_DEBUG_OBJECT (vesdeliver,
        "Input buffer %p with size: %" G_GSIZE_FORMAT ", timestamp: %" G_GUINT64_FORMAT
        ", offset: %" G_GUINT64_FORMAT, inbuf, input_map.size,
        GST_BUFFER_TIMESTAMP (inbuf), GST_BUFFER_OFFSET (inbuf));

  out_mem = gst_buffer_peek_memory (outbuf, 0);
  if (gst_is_dmabuf_memory (out_mem)) {
    buf_fd = gst_dmabuf_memory_get_fd (out_mem);
    fd_memory_size = gst_memory_get_sizes (out_mem, NULL, NULL);
  } else {
    GST_ERROR ("Invalide gst buffer type");
    return GST_FLOW_ERROR;
  }

  if (vesdeliver->secure) {
    uint32_t bytes_copied = 0;
    int ret = SECURE_COPY_RETURN_SUCCESS;

    ret = vesdeliver->Content_Protection_Copy (vesdeliver->secure_handle, input_map.data,
              input_map.size, buf_fd, 0, &bytes_copied, SECURE_COPY_NONSECURE_TO_SECURE);
    if (ret == SECURE_COPY_RETURN_SUCCESS) {
      GST_DEBUG_OBJECT (vesdeliver, "secure copy input size: %" G_GSIZE_FORMAT ", sec buf_fd: %d, "
            "bytes copied: %u", input_map.size, buf_fd, bytes_copied);
    } else {
      GST_ERROR ("Content_Protection_Copy failed with %d", ret);
    }
  } else {
    void *ptr = NULL;
    ptr = mmap (NULL, fd_memory_size, PROT_READ | PROT_WRITE, MAP_SHARED, buf_fd, 0);
    if (ptr != MAP_FAILED) {
      memcpy (ptr, input_map.data, input_map.size);
      GST_DEBUG_OBJECT (vesdeliver, "memcpy %" G_GSIZE_FORMAT " bytes to %p with buf_fd: %d, size: %"
            G_GSIZE_FORMAT, input_map.size, ptr, buf_fd, fd_memory_size);
      munmap (ptr, fd_memory_size);
    }
  }

  gst_buffer_unmap (inbuf, &input_map);

  return GST_FLOW_OK;
}

static void
gst_vesdeliver_set_property(GObject* object, guint property_id,
    const GValue* value, GParamSpec* pspec)
{
  GstVesDeliver* self = GST_VESDELIVER(object);

  switch (property_id) {
    case PROP_SECURE:
      self->secure = g_value_get_boolean (value);
      GST_DEBUG_OBJECT (self, "secure mode: %s", self->secure ? "TRUE" : "FALSE");
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
      break;
  }
}

static void
gst_vesdeliver_get_property(GObject* object, guint property_id,
    GValue* value, GParamSpec* pspec)
{
  GstVesDeliver* self = GST_VESDELIVER (object);

  switch (property_id) {
    case PROP_SECURE:
      g_value_set_boolean (value, self->secure);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
vesdeliver_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (vesdeliver_debug, "vesdeliver", 0,
      "QTI Video Element Stream Plugin");

  return gst_element_register (plugin, "vesdeliver",
      GST_RANK_SECONDARY, GST_TYPE_VESDELIVER);
}

/* gstreamer looks for this structure to register vesdeliver */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vesdeliver,
    "QTI Video Element Stream Plugin",
    vesdeliver_init,
    PACKAGE_VERSION,
    GST_LICENSE_UNKNOWN,
    PACKAGE_NAME,
    "-")
