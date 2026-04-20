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
#include <dlfcn.h>
#include "gstvesdeliverallocator.h"
#include "gstvesdeliver.h"

/* Dynamically load libs by dlopen. */
static const char *crypto_lib_name = "libcontentcopy.so";
static const char *crypto_app_name = "smpcpyap64";
#ifdef USE_DMAHEAP
static const char *vmmem_lib_name = "libvmmem.so.0";
static const char *vm_name = "qcom,cp_bitstream";
#define LEND_VM_NUM 1
#endif

GST_DEBUG_CATEGORY (vesdeliver_debug);
#define GST_CAT_DEFAULT vesdeliver_debug
#define THRESHOLD_ALLOC_BUFFER_COUNT 30
#define THRESHOLD_ALLOC_BUFFER_COUNT_REVISED 12

enum
{
  PROP_0,
  PROP_SECURE,
  PROP_BUF_RECYCLE,
  PROP_BUF_CONTIGUOUS,
  PROP_TRANSFORM_CAPS,
  PROP_MIN_OUTPUT_BUF_SIZE,
};

static GstStaticPadTemplate sink_tmpl = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (H264_CAPS ";" H265_CAPS ";" VP8_CAPS ";" VP9_CAPS ";" MPEG2_CAPS ";"
        AV1_CAPS ";" PLAYREADY_CENC_H264_CAPS ";" WIDEVINE_CENC_H264_CAPS ";"
        PLAYREADY_CENC_H265_CAPS ";" WIDEVINE_CENC_H265_CAPS ";" VIDEO_RAW_DMABUF_CAPS));

static GstStaticPadTemplate src_tmpl = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (H264_CAPS ";" H265_CAPS ";" VP8_CAPS ";" VP9_CAPS ";" MPEG2_CAPS ";"
        AV1_CAPS ";" PLAYREADY_CENC_H264_CAPS ";" WIDEVINE_CENC_H264_CAPS ";"
        PLAYREADY_CENC_H265_CAPS ";" WIDEVINE_CENC_H265_CAPS ";" VIDEO_RAW_CAPS));

#define gst_vesdeliver_parent_class parent_class
G_DEFINE_TYPE (GstVesDeliver, gst_vesdeliver, GST_TYPE_BASE_TRANSFORM);

#define SECURE_COPY_RETURN_SUCCESS      0
#define SECURE_COPY_NONSECURE_TO_SECURE 0
#define MIN_OUTPUT_BUF_PROP_NO_LIMIT    0
#define MIN_OUTPUT_BUF_PROP_CALCULATE   -1
#define MIN_OUTPUT_BUF_PROP_DEFAULT MIN_OUTPUT_BUF_PROP_NO_LIMIT
#define MB_SIZE_IN_PIXEL                (16 * 16)
#define NUM_MBS_PER_FRAME(__width, __height) \
    (((__width + 15) >> 4) * ((__height + 15) >> 4))
#define NUM_MBS_4k NUM_MBS_PER_FRAME(4096, 2304)
#define NUM_MBS_8k NUM_MBS_PER_FRAME(8192, 4320)
#define GST_TYPE_VESDELIVER_SECURE_MODE (gst_vesdeliver_secure_mode_get_type ())
#define GST_TYPE_VESDELIVER_TRANSFORM_CAPS (gst_vesdeliver_transform_caps_get_type ())

static GstFlowReturn
gst_vesdeliver_prepare_output_buffer (GstBaseTransform * base,
    GstBuffer * inbuffer, GstBuffer ** outbuffer);
static GstFlowReturn
gst_vesdeliver_transform (GstBaseTransform * trans, GstBuffer * inbuffer,
    GstBuffer * outbuffer);
static void
gst_vesdeliver_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec);
static void
gst_vesdeliver_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_vesdeliver_start (GstBaseTransform * trans);
static gboolean gst_vesdeliver_stop (GstBaseTransform * trans);
static GstCaps *gst_vesdeliver_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_vesdeliver_set_caps (GstBaseTransform * trans,
    GstCaps * in_caps, GstCaps * out_caps);

static GType
gst_vesdeliver_secure_mode_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {SECURE_DISABLE, "Non-secure mode", "disable"},
      {SECURE_COPY, "Secure copy mode", "secure-copy"},
      {LEND_DMABUF, "Lend dmabuf or ionbuf mode", "lend-dmabuf"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstVesdeliverSecureMode", values);
  }
  return qtype;
}

static GType
gst_vesdeliver_transform_caps_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {TRANSFORM_DISABLE, "Do not transform caps", "disable"},
      {TRANSFORM_CENC_TO_CLEAR, "Transform caps from CENC to clear",
          "cenc-to-clear"},
      {TRANSFORM_CLEAR_TO_CENC, "Transform caps from clear to CENC",
          "clear-to-cenc"},
      {TRANSFORM_RAWVIDEODMA_TO_RAWVIDEO, "Transform caps from dmabuf raw video to common raw video",
          "dmav-to-rawv"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstVesdeliverTransformCaps", values);
  }
  return qtype;
}

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

  g_object_class_install_property (gobject_class,
      PROP_SECURE,
      g_param_spec_enum ("secure", "Secure Mode",
          "Specify the secure mode",
          GST_TYPE_VESDELIVER_SECURE_MODE,
          SECURE_DISABLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class,
      PROP_BUF_RECYCLE,
      g_param_spec_boolean ("buf-recycle", "Buffer Recycle",
          "Enable output DMA buffer recycle",
          TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class,
      PROP_BUF_CONTIGUOUS,
      g_param_spec_boolean ("buf-contiguous", "Buffer Contiguous",
          "If enabled, will allocate physical contiguous DMA memory for bitstream buffer, "
          "only work in lend dmabuf mode",
          TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class,
      PROP_TRANSFORM_CAPS,
      g_param_spec_enum ("transform-caps", "Transform Caps",
          "Transform the caps of sink and source pad, the buffer will pass through intact",
          GST_TYPE_VESDELIVER_TRANSFORM_CAPS,
          TRANSFORM_DISABLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class,
      PROP_MIN_OUTPUT_BUF_SIZE,
      g_param_spec_int ("min-output-buf-size", "Minimum Output Buffer Size",
          "Set the minimum output buffer size. A value of 0 indicates no limit. While -1 "
          "means to calculate the size based on the decoding bitstream buffer requirements "
          "of the Gen3 kernel driver, considering different codecs, resolutions, and security "
          "factors. Positive values specify a fixed minimum buffer size.",
          G_MININT, G_MAXINT, MIN_OUTPUT_BUF_PROP_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  gstbasetrans_class->transform = GST_DEBUG_FUNCPTR (gst_vesdeliver_transform);
  gstbasetrans_class->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_vesdeliver_prepare_output_buffer);
  gstbasetrans_class->start = GST_DEBUG_FUNCPTR (gst_vesdeliver_start);
  gstbasetrans_class->stop = GST_DEBUG_FUNCPTR (gst_vesdeliver_stop);
  gstbasetrans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_vesdeliver_transform_caps);
  gstbasetrans_class->set_caps = GST_DEBUG_FUNCPTR (gst_vesdeliver_set_caps);

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
  vesdeliver->secure = SECURE_DISABLE;
  vesdeliver->buf_recycle = TRUE;
  vesdeliver->buf_contiguous = TRUE;
  vesdeliver->allocator = NULL;
  vesdeliver->secure_handle = NULL;
  vesdeliver->transform_caps = TRANSFORM_DISABLE;
  vesdeliver->min_output_buf_size = MIN_OUTPUT_BUF_PROP_DEFAULT;
  vesdeliver->input_format = NULL;
  vesdeliver->input_width = 0;
  vesdeliver->input_height = 0;

  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (vesdeliver), FALSE);
}

static gboolean
gst_vesdeliver_start (GstBaseTransform * trans)
{
  GstVesDeliver *vesdeliver = GST_VESDELIVER (trans);
  gboolean status = FALSE;

  if (TRANSFORM_DISABLE != vesdeliver->transform_caps) {
    /* allocator is not needed if enable transform caps */
    return TRUE;
  }

  if (LEND_DMABUF == vesdeliver->secure) {
#ifdef USE_DMAHEAP
    vesdeliver->vmmem_lib_handle = dlopen (vmmem_lib_name, RTLD_NOW);
    if (NULL == vesdeliver->vmmem_lib_handle) {
      const char *dlerr = dlerror ();
      if (NULL == dlerr)
        dlerr = "NULL";
      GST_ERROR_OBJECT (vesdeliver, "dlopen %s error: %s", vmmem_lib_name,
          dlerr);
      goto exit;
    }
    vesdeliver->CreateVmMem =
        dlsym (vesdeliver->vmmem_lib_handle, "CreateVmMem");
    vesdeliver->FreeVmMem = dlsym (vesdeliver->vmmem_lib_handle, "FreeVmMem");
    vesdeliver->IsExclusiveOwnerDmabuf =
        dlsym (vesdeliver->vmmem_lib_handle, "IsExclusiveOwnerDmabuf");
    vesdeliver->FindVmByName =
        dlsym (vesdeliver->vmmem_lib_handle, "FindVmByName");
    vesdeliver->LendDmabuf = dlsym (vesdeliver->vmmem_lib_handle, "LendDmabuf");
    vesdeliver->ReclaimDmabuf =
        dlsym (vesdeliver->vmmem_lib_handle, "ReclaimDmabuf");

    if (!vesdeliver->CreateVmMem ||
        !vesdeliver->FreeVmMem ||
        !vesdeliver->IsExclusiveOwnerDmabuf ||
        !vesdeliver->FindVmByName || !vesdeliver->LendDmabuf
        || !vesdeliver->ReclaimDmabuf) {
      GST_ERROR_OBJECT (vesdeliver, "dlsym failed with NULL symbol");
      dlclose (vesdeliver->vmmem_lib_handle);
      vesdeliver->vmmem_lib_handle = NULL;
      goto exit;
    }

    GST_INFO_OBJECT (vesdeliver, "open %s(%p) successfully",
        vmmem_lib_name, vesdeliver->vmmem_lib_handle);
    vesdeliver->vm_instance = vesdeliver->CreateVmMem ();
    if (vesdeliver->vm_instance == NULL) {
      GST_ERROR_OBJECT (vesdeliver, "Failed to CreateVmMem instance");
      dlclose (vesdeliver->vmmem_lib_handle);
      vesdeliver->vmmem_lib_handle = NULL;
      goto exit;
    }
    vesdeliver->vm_handle =
        vesdeliver->FindVmByName (vesdeliver->vm_instance, (char *) vm_name);
    if (vesdeliver->vm_handle < 0) {
      GST_ERROR_OBJECT (vesdeliver, "Failed to find VM by name %s", vm_name);
      vesdeliver->FreeVmMem (vesdeliver->vm_instance);
      vesdeliver->vm_instance = NULL;
      dlclose (vesdeliver->vmmem_lib_handle);
      vesdeliver->vmmem_lib_handle = NULL;
      goto exit;
    }
#endif
  } else if (SECURE_COPY == vesdeliver->secure) {
#ifdef DISABLE_SECURE_COPY
    GST_ERROR_OBJECT (vesdeliver, "secure copy mode is not supported!");
    g_warn_if_fail (FALSE && "secure copy mode is not supported!");
    return FALSE;
#endif
    vesdeliver->crypto_handle = dlopen (crypto_lib_name, RTLD_NOW);
    if (NULL == vesdeliver->crypto_handle) {
      const char *dlerr = dlerror ();
      if (NULL == dlerr)
        dlerr = "NULL";
      GST_ERROR_OBJECT (vesdeliver, "dlopen %s error: %s", crypto_lib_name,
          dlerr);
      goto exit;
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
      GST_ERROR_OBJECT (vesdeliver, "dlsym failed with NULL symbol");
      dlclose (vesdeliver->crypto_handle);
      vesdeliver->crypto_handle = NULL;
      goto exit;
    }

    GST_INFO_OBJECT (vesdeliver, "open %s(%p) successfully",
        crypto_lib_name, vesdeliver->crypto_handle);
    int ret = SECURE_COPY_RETURN_SUCCESS;
    ret = vesdeliver->Content_Protection_Copy_Init (&vesdeliver->secure_handle);
    if (ret != SECURE_COPY_RETURN_SUCCESS) {
      GST_ERROR_OBJECT (vesdeliver,
          "Content_Protection_Copy_Init failed with %d", ret);
      dlclose (vesdeliver->crypto_handle);
      vesdeliver->crypto_handle = NULL;
      goto exit;
    }

    ret = vesdeliver->Content_Protection_Set_AppName (crypto_app_name);
    if (ret != SECURE_COPY_RETURN_SUCCESS) {
      GST_ERROR_OBJECT (vesdeliver,
          "Content_Protection_Set_AppName failed with %d", ret);
      vesdeliver->
          Content_Protection_Copy_Terminate (&vesdeliver->secure_handle);
      dlclose (vesdeliver->crypto_handle);
      vesdeliver->crypto_handle = NULL;
      goto exit;
    }
  }

  if (NULL == vesdeliver->allocator) {
    AllocatorParameter param;
    memset (&param, 0, sizeof (AllocatorParameter));

    param.secure_mode = vesdeliver->secure;
    param.buf_recycle = vesdeliver->buf_recycle;
    param.buf_contiguous = vesdeliver->buf_contiguous;
#ifdef USE_DMAHEAP
    param.vm_instance = vesdeliver->vm_instance;
    param.ReclaimDmabuf = vesdeliver->ReclaimDmabuf;
#endif
    vesdeliver->allocator = gst_vesdeliver_allocator_new (&param);
    GST_DEBUG_OBJECT (vesdeliver, "Create vesdeliver allocator");
    g_return_val_if_fail (vesdeliver->allocator != NULL, FALSE);
    status = TRUE;
  }

exit:
  return status;
}

static gboolean
gst_vesdeliver_stop (GstBaseTransform * trans)
{
  GstVesDeliver *vesdeliver = GST_VESDELIVER (trans);

  if (LEND_DMABUF == vesdeliver->secure) {
#ifdef USE_DMAHEAP
    if (vesdeliver->vm_instance != NULL) {
      vesdeliver->FreeVmMem (vesdeliver->vm_instance);
      vesdeliver->vm_instance = NULL;
      GST_INFO_OBJECT (vesdeliver, "FreeVmMem successfully");
    }
    if (vesdeliver->vmmem_lib_handle) {
      GST_INFO_OBJECT (vesdeliver, "dlclose %s(%p)", vmmem_lib_name,
          vesdeliver->vmmem_lib_handle);
      dlclose (vesdeliver->vmmem_lib_handle);
      vesdeliver->vmmem_lib_handle = NULL;
    }
#endif
  } else if (SECURE_COPY == vesdeliver->secure) {
    if (vesdeliver->secure_handle) {
      int ret = SECURE_COPY_RETURN_SUCCESS;
      ret =
          vesdeliver->
          Content_Protection_Copy_Terminate (&vesdeliver->secure_handle);
      if (ret != SECURE_COPY_RETURN_SUCCESS) {
        GST_ERROR_OBJECT (vesdeliver,
            "Content_Protection_Copy_Terminate failed with %d", ret);
      }
    }
    if (vesdeliver->crypto_handle) {
      GST_INFO_OBJECT (vesdeliver, "dlclose %s(%p)", crypto_lib_name,
          vesdeliver->crypto_handle);
      dlclose (vesdeliver->crypto_handle);
      vesdeliver->crypto_handle = NULL;
    }
  }

  if (vesdeliver->allocator) {
    gst_object_unref (vesdeliver->allocator);
    vesdeliver->allocator = NULL;
    GST_DEBUG_OBJECT (vesdeliver, "Unref vesdeliver allocator");
  }

  if (vesdeliver->input_format) {
    g_free (vesdeliver->input_format);
    vesdeliver->input_format = NULL;
  }

  return TRUE;
}

static guint
calculate_min_output_buffer_size (GstVesDeliver *vesdeliver)
{
  guint frame_size;
  guint div_factor = 1;
  guint base_res_mbs = NUM_MBS_4k;

  if (NUM_MBS_PER_FRAME(vesdeliver->input_width, vesdeliver->input_height) > base_res_mbs) {
    div_factor = 4;
    base_res_mbs = NUM_MBS_8k;
  } else {
    if (g_strcmp0 (vesdeliver->input_format, "video/x-vp8") == 0 ||
        g_strcmp0 (vesdeliver->input_format, "video/x-vp9") == 0) {
      div_factor = 1;
    } else {
      div_factor = 2;
    }
  }

  if (vesdeliver->secure != SECURE_DISABLE) {
    div_factor = div_factor << 1;
  }

  frame_size = base_res_mbs * MB_SIZE_IN_PIXEL * 3 / 2 / div_factor;

  // multiply by 10/8 (1.25) to get size to cover possible 10 bit case h265 and vp9
  if (g_strcmp0 (vesdeliver->input_format, "video/x-h265") == 0 ||
      g_strcmp0 (vesdeliver->input_format, "video/x-vp9") == 0) {
    frame_size = frame_size + (frame_size >> 2);
  }

  return GST_ROUND_UP_N(frame_size, 4096);
}

static GstFlowReturn
gst_vesdeliver_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstVesDeliver *vesdeliver = GST_VESDELIVER (trans);
  gsize inbuf_size = gst_buffer_get_size (inbuf);
  gsize outbuf_size = 0;

  if (TRANSFORM_DISABLE == vesdeliver->transform_caps) {
    switch (vesdeliver->min_output_buf_size) {
      case MIN_OUTPUT_BUF_PROP_NO_LIMIT:
        outbuf_size = inbuf_size;
        break;
      case MIN_OUTPUT_BUF_PROP_CALCULATE:
        outbuf_size = calculate_min_output_buffer_size (vesdeliver);
        GST_INFO_OBJECT (vesdeliver, "calculate minimum output buffer size as %zu",
            outbuf_size);
        break;
      default:
        if (vesdeliver->min_output_buf_size < 0) {
          GST_ERROR_OBJECT (vesdeliver, "Invalid output buffer size: %d",
              vesdeliver->min_output_buf_size);
          return GST_FLOW_ERROR;
        }
        outbuf_size = vesdeliver->min_output_buf_size;
        break;
    }

    // ensure outbuf_size is at least the size of the input buffer
    if (outbuf_size < inbuf_size) {
      outbuf_size = inbuf_size;
      GST_INFO_OBJECT (vesdeliver, "Update output buffer size to input buffer size %zu",
          outbuf_size);
    }

    *outbuf = gst_buffer_new_allocate (vesdeliver->allocator,
        outbuf_size, NULL);

    g_return_val_if_fail (*outbuf != NULL, GST_FLOW_ERROR);
    GST_BASE_TRANSFORM_CLASS (parent_class)->copy_metadata (trans, inbuf,
        *outbuf);
  } else {
    /* The incoming buffer will not be modified, so just reuse it */
    *outbuf = inbuf;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vesdeliver_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstFlowReturn status = GST_FLOW_OK;
  GstVesDeliver *vesdeliver = GST_VESDELIVER (trans);
  GstMemory *out_mem = NULL;
  int buf_fd = -1;

  if (TRANSFORM_DISABLE != vesdeliver->transform_caps) {
    gsize buf_sz = 0;
    gsize len = gst_buffer_get_sizes (inbuf, NULL, &buf_sz);
    GST_LOG_OBJECT (vesdeliver, "Input buf %p with len %" G_GSIZE_FORMAT ", sz %"
        G_GSIZE_FORMAT ", pts %" GST_TIME_FORMAT ", only caps change", inbuf, len,
        buf_sz, GST_TIME_ARGS (GST_BUFFER_PTS (inbuf)));
    return GST_FLOW_OK;
  }

  g_return_val_if_fail (gst_buffer_is_writable (outbuf), GST_FLOW_ERROR);

  GstMapInfo input_map = { };
  gst_buffer_map (inbuf, &input_map, GST_MAP_READ);
  GST_DEBUG_OBJECT (vesdeliver,
      "Input buffer %p with len: %" G_GSIZE_FORMAT ", timestamp: %"
      GST_TIME_FORMAT ", offset: %" G_GUINT64_FORMAT, inbuf, input_map.size,
      GST_TIME_ARGS (GST_BUFFER_PTS (inbuf)), GST_BUFFER_OFFSET (inbuf));

  out_mem = gst_buffer_peek_memory (outbuf, 0);
  if (gst_is_dmabuf_memory (out_mem)) {
    buf_fd = gst_dmabuf_memory_get_fd (out_mem);
  } else {
    GST_ERROR_OBJECT (vesdeliver, "Invalide gst buffer type");
    status = GST_FLOW_ERROR;
    goto exit;
  }

  if (SECURE_COPY == vesdeliver->secure) {
    uint32_t bytes_copied = 0;
    int ret = SECURE_COPY_RETURN_SUCCESS;

    ret =
        vesdeliver->Content_Protection_Copy (vesdeliver->secure_handle,
        input_map.data, input_map.size, buf_fd, 0, &bytes_copied,
        SECURE_COPY_NONSECURE_TO_SECURE);
    if (ret == SECURE_COPY_RETURN_SUCCESS) {
      GST_DEBUG_OBJECT (vesdeliver,
          "secure copy input size: %" G_GSIZE_FORMAT ", sec buf_fd: %d, "
          "bytes copied: %u", input_map.size, buf_fd, bytes_copied);
    } else {
      GST_ERROR_OBJECT (vesdeliver, "Content_Protection_Copy failed with %d",
          ret);
      status = GST_FLOW_ERROR;
    }
  } else {
    void *ptr = NULL;
    ptr =
        mmap (NULL, input_map.size, PROT_READ | PROT_WRITE, MAP_SHARED, buf_fd,
        0);
    if (ptr != MAP_FAILED) {
      memcpy (ptr, input_map.data, input_map.size);
      GST_DEBUG_OBJECT (vesdeliver,
          "memcpy %" G_GSIZE_FORMAT " bytes to %p with buf_fd: %d",
          input_map.size, ptr, buf_fd);
      munmap (ptr, input_map.size);
      gst_memory_resize (out_mem, 0, input_map.size);
    } else {
      GST_ERROR_OBJECT (vesdeliver, "mmap failed(%s) for buf_fd:%d",
          strerror (errno), buf_fd);
      status = GST_FLOW_ERROR;
      goto exit;
    }
#ifdef USE_DMAHEAP
    if (LEND_DMABUF == vesdeliver->secure) {
      bool is_exclusive_owner = TRUE;
      if (0 != vesdeliver->IsExclusiveOwnerDmabuf (buf_fd, &is_exclusive_owner)) {
        GST_WARNING_OBJECT (vesdeliver,
            "Failed to check IsExclusiveOwnerDmabuf");
      }
      if (is_exclusive_owner) {
        VmHandle vmHandleArr[LEND_VM_NUM] = { vesdeliver->vm_handle };
        uint32_t permArr[LEND_VM_NUM] = { VMMEM_READ | VMMEM_WRITE };
        int ret = -1;

        GST_DEBUG_OBJECT (vesdeliver, "Lend dmabuf with fd=%d is calling", buf_fd);
        ret =
            vesdeliver->LendDmabuf (vesdeliver->vm_instance, buf_fd,
            vmHandleArr, permArr, LEND_VM_NUM);

        if (ret < 0) {
          GST_ERROR_OBJECT (vesdeliver, "Failed to lend dmabuf, fd=%d ret=%d",
              buf_fd, ret);
          status = GST_FLOW_ERROR;
        } else if (ret == 0) {
          GST_DEBUG_OBJECT (vesdeliver, "Lend dmabuf with fd=%d successfully",
              buf_fd);
        }
      } else {
        GST_WARNING_OBJECT (vesdeliver, "The dmabuf is not exclusive owned");
      }
    }
#else
#ifdef ION_FLAG_ION_LEND_BUF
    if (LEND_DMABUF == vesdeliver->secure) {
      if (vesdeliver->allocator) {
        int ret = -1;
        GstVesDeliverAllocator *alloc = GST_VESDELIVER_ALLOCATOR (vesdeliver->allocator);
        ret = alloc->ion_lend_buf(alloc->ion_fd, buf_fd,
                          ION_VMID_CP_BITSTREAM, ION_PERM_READ | ION_PERM_WRITE);
        if (ret != 0) {
          GST_ERROR_OBJECT(vesdeliver, "Failed to lend ionbuf, buf_fd=%d ret=%d",
              buf_fd, ret);
        } else {
          GST_DEBUG_OBJECT (vesdeliver, "Lend ionbuf with buf_fd=%d successfully.",
              buf_fd);
        }
      } else {
        GST_ERROR_OBJECT(vesdeliver, "There is no allocator to do buffer lending.");
      }
    }
#endif
#endif
  }

exit:
  gst_buffer_unmap (inbuf, &input_map);

  return status;
}

static void
gst_vesdeliver_caps_append_if_not_included (GstCaps * destination,
    GstStructure * structure)
{
  guint caps_size;
  gboolean included = FALSE;

  caps_size = gst_caps_get_size (destination);
  for (guint index = 0; !included && index < caps_size; ++index) {
    GstStructure *s = gst_caps_get_structure (destination, index);
    if (gst_structure_is_equal (s, structure)) {
      included = TRUE;
    }
  }
  if (!included) {
    gst_caps_append_structure (destination, structure);
  } else {
    gst_structure_free (structure);
  }
}

static void
gst_vesdeliver_transform_caps_from_cenc_to_clear (GstCaps * transformed_caps,
    GstStructure * in)
{
  GstStructure *out = NULL;
  const gchar *media_string = NULL;

  out = gst_structure_copy (in);
  media_string = gst_structure_get_string (out, "original-media-type");
  if (media_string) {
    gst_structure_set_name (out, media_string);
  }
  gst_vesdeliver_caps_append_if_not_included (transformed_caps, out);
}

static void
gst_vesdeliver_transform_caps_from_clear_to_cenc (GstCaps * transformed_caps,
    GstStructure * in, gboolean need_bytestream)
{
  const gchar *name;
  GstStructure *out = NULL;

  name = gst_structure_get_name (in);
  if (strcmp (name, "application/x-cenc")) {
    const char *supported_protection[] = {
      PLAYREADY_PROTECTION_SYSTEM_ID,
      WIDEVINE_PROTECTION_SYSTEM_ID,
      NULL
    };
    for (int i = 0; supported_protection[i] != NULL; i++) {
      out = gst_structure_copy (in);
      gst_structure_set (out,
          "protection-system", G_TYPE_STRING, supported_protection[i],
          "original-media-type", G_TYPE_STRING, name, NULL);
      gst_structure_set_name (out, "application/x-cenc");
      if (need_bytestream) {
        gst_structure_set (out, "stream-format", G_TYPE_STRING, "byte-stream",
            NULL);
      }
      gst_vesdeliver_caps_append_if_not_included (transformed_caps, out);
    }
  }
}

static void
gst_vesdeliver_transform_caps_from_dmav_to_rawv (GstVesDeliver * vesdeliver,
    GstCaps * transformed_caps, GstStructure * in, GstCapsFeatures * f)
{
  if (f == NULL) {
    GST_ERROR_OBJECT (vesdeliver, "video/x-raw has no feature, it's not expected!");
  } else {
    const char* str = gst_caps_features_to_string (f);
    if (str && !strcmp (str, "memory:DMABuf")) {
      GST_DEBUG_OBJECT (vesdeliver, "from dmav to rawv, remove feature memory:DMABuf");
      gst_caps_append_structure (transformed_caps, gst_structure_copy(in));
    } else {
      GST_ERROR_OBJECT (vesdeliver, "Get string %s from feature %p, not expected, expect memory:DMABuf", str==NULL?"null":str, f);
    }
    g_free(str);
  }
  return;
}

static void
gst_vesdeliver_transform_caps_from_rawv_to_dmav (GstVesDeliver * vesdeliver,
    GstCaps * transformed_caps, GstStructure * in)
{
  GST_DEBUG_OBJECT (vesdeliver, "from rawv to dmav, add feature memory:DMABuf");
  gst_caps_append_structure_full (transformed_caps, gst_structure_copy(in), gst_caps_features_from_string("memory:DMABuf"));
  return;
}

static GstCaps *
gst_vesdeliver_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstVesDeliver *vesdeliver = GST_VESDELIVER (trans);
  GstCaps *transformed_caps = NULL;
  guint caps_size;
  const gchar *name = NULL;

  if (TRANSFORM_DISABLE == vesdeliver->transform_caps) {
    GST_DEBUG_OBJECT (vesdeliver,
        "direction=%d, identity from: %" GST_PTR_FORMAT, direction, caps);
    /* no transform function, use the identity transform */
    if (filter) {
      transformed_caps =
          gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    } else {
      transformed_caps = gst_caps_ref (caps);
    }
    return transformed_caps;
  }

  GST_DEBUG_OBJECT (vesdeliver,
      "Transforming caps %" GST_PTR_FORMAT " in direction %s", caps,
      (direction == GST_PAD_SINK) ? "sink" : "src");

  transformed_caps = gst_caps_new_empty ();
  caps_size = gst_caps_get_size (caps);
  for (guint index = 0; index < caps_size; ++index) {
    GstStructure *in = gst_caps_get_structure (caps, index);
    name = gst_structure_get_name (in);

    if (name && !strcmp(name, "video/x-raw")) {
      if (TRANSFORM_RAWVIDEODMA_TO_RAWVIDEO == vesdeliver->transform_caps) {
        if (direction == GST_PAD_SINK) {
          gst_vesdeliver_transform_caps_from_dmav_to_rawv (vesdeliver, transformed_caps, in, gst_caps_get_features(caps, index));
        } else if (direction == GST_PAD_SRC) {
          gst_vesdeliver_transform_caps_from_rawv_to_dmav (vesdeliver, transformed_caps, in);
        } else {
          GST_ERROR_OBJECT (vesdeliver, "error direction %d", direction);
        }
        //just handle video/x-raw, needn't handle other mime_types, ignore them
        break;
      } else {
        //if TRANSFORM_CENC_TO_CLEAR or TRANSFORM_CLEAR_TO_CENC, ignore video/x-raw
        continue;
      }
    } else {//mime_type is not video/x-raw
      if (TRANSFORM_RAWVIDEODMA_TO_RAWVIDEO == vesdeliver->transform_caps) {
        //if TRANSFORM_RAWVIDEODMA_TO_RAWVIDEO, ignore mime_types which are not video/x-raw
        continue;
      }
    }

    if (direction == GST_PAD_SINK) {
      if (TRANSFORM_CENC_TO_CLEAR == vesdeliver->transform_caps) {
        /* downstream is parser */
        gst_vesdeliver_transform_caps_from_cenc_to_clear (transformed_caps, in);
      } else if (TRANSFORM_CLEAR_TO_CENC == vesdeliver->transform_caps) {
        /* downstream is decryptor */
        gst_vesdeliver_transform_caps_from_clear_to_cenc (transformed_caps, in,
            TRUE);
      }
    } else if (direction == GST_PAD_SRC) {
      if (TRANSFORM_CENC_TO_CLEAR == vesdeliver->transform_caps) {
        /* upstream is demux */
        gst_vesdeliver_transform_caps_from_clear_to_cenc (transformed_caps, in,
            FALSE);
      } else if (TRANSFORM_CLEAR_TO_CENC == vesdeliver->transform_caps) {
        /* upstream is parser */
        gst_vesdeliver_transform_caps_from_cenc_to_clear (transformed_caps, in);
      }
    } else {
      GST_ERROR_OBJECT (vesdeliver, "Invalid direction %d", direction);
    }
  }

  if (filter) {
    GstCaps *intersection;
    GST_DEBUG_OBJECT (vesdeliver, "Using filter caps %" GST_PTR_FORMAT, filter);
    intersection =
        gst_caps_intersect_full (transformed_caps, filter,
        GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (transformed_caps);
    transformed_caps = intersection;
  }

  GST_DEBUG_OBJECT (vesdeliver,
      "Return transformed caps %" GST_PTR_FORMAT " in direction %s",
      transformed_caps, (direction == GST_PAD_SINK) ? "sink" : "src");

  return transformed_caps;
}

static gboolean
gst_vesdeliver_set_caps (GstBaseTransform * trans, GstCaps * in_caps, GstCaps * out_caps)
{
  GstVesDeliver *vesdeliver = GST_VESDELIVER (trans);
  GstStructure *in_structure;
  const gchar *format;
  gint width, height;

  in_structure = gst_caps_get_structure (in_caps, 0);

  format = gst_structure_get_name (in_structure);

  if (!gst_structure_get_int (in_structure, "width", &width) ||
      !gst_structure_get_int (in_structure, "height", &height)) {
    GST_ERROR_OBJECT (vesdeliver, "Failed to get width and height from caps");
    return FALSE;
  }

  GST_INFO_OBJECT (vesdeliver, "Get input format: %s, width: %d, height: %d from caps",
      format, width, height);

  if (vesdeliver->input_format) {
    g_free (vesdeliver->input_format);
  }
  vesdeliver->input_format = g_strdup (format);
  vesdeliver->input_width = width;
  vesdeliver->input_height = height;

  /* Update allocator param.
   * Set threshold_buf_count to THRESHOLD_ALLOC_BUFFER_COUNT_REVISED if secure mode
   * is LEND_DMABUF and resolution is more than 2560*1440.
   */
  if (vesdeliver->allocator) {
    GstVesDeliverAllocator *alloc = GST_VESDELIVER_ALLOCATOR (vesdeliver->allocator);
    alloc->param.threshold_buf_count = THRESHOLD_ALLOC_BUFFER_COUNT;
    if (alloc->param.secure_mode == LEND_DMABUF
        && vesdeliver->input_width * vesdeliver->input_height > 2560*1440) {
      alloc->param.threshold_buf_count = THRESHOLD_ALLOC_BUFFER_COUNT_REVISED;
    }

    GST_INFO_OBJECT (vesdeliver, "set threshold_buf_count to %d",
        alloc->param.threshold_buf_count);
  }

  return TRUE;
}

static void
gst_vesdeliver_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVesDeliver *self = GST_VESDELIVER (object);

  switch (property_id) {
    case PROP_SECURE:
      self->secure = g_value_get_enum (value);
      GST_DEBUG_OBJECT (self, "secure mode: %d", self->secure);
      break;
    case PROP_BUF_RECYCLE:
      self->buf_recycle = g_value_get_boolean (value);
      break;
    case PROP_BUF_CONTIGUOUS:
      self->buf_contiguous = g_value_get_boolean (value);
      break;
    case PROP_TRANSFORM_CAPS:
      self->transform_caps = g_value_get_enum (value);
      break;
    case PROP_MIN_OUTPUT_BUF_SIZE:
      self->min_output_buf_size = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_vesdeliver_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstVesDeliver *self = GST_VESDELIVER (object);

  switch (property_id) {
    case PROP_SECURE:
      g_value_set_enum (value, self->secure);
      break;
    case PROP_BUF_RECYCLE:
      g_value_set_boolean (value, self->buf_recycle);
      break;
    case PROP_BUF_CONTIGUOUS:
      g_value_set_boolean (value, self->buf_contiguous);
      break;
    case PROP_TRANSFORM_CAPS:
      g_value_set_enum (value, self->transform_caps);
      break;
    case PROP_MIN_OUTPUT_BUF_SIZE:
      g_value_set_int (value, self->min_output_buf_size);
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
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vesdeliver,
    "QTI Video Element Stream Plugin",
    vesdeliver_init, PACKAGE_VERSION "-" G_STRINGIFY(GST_VERSION_MAJOR) "/" G_STRINGIFY(GST_VERSION_MINOR) "/" G_STRINGIFY(GST_VERSION_MICRO), GST_LICENSE_UNKNOWN, PACKAGE_NAME, "-")
