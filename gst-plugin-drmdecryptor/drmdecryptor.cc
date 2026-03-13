/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "drmdecryptor.h"

#include <gst/memory/gstmempool.h>
#include <dlfcn.h>
#include <cstdint>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <memory>
#include <vmmem/vmmem.h>

#define GST_CAT_DEFAULT decryptor_debug
GST_DEBUG_CATEGORY_STATIC (decryptor_debug);

#define gst_drm_decryptor_parent_class parent_class
G_DEFINE_TYPE (GstDrmDecryptor, gst_drm_decryptor, GST_TYPE_ELEMENT);

#define DEFAULT_PROP_SESSION_ID     NULL
// TODO: Check with SSG team to have a common macro or fetch the buffer size
// requirements from prdrmengine lib
#define DEFAULT_BUFFER_SIZE       (1024*1024*2)
#define DEFAULT_MIN_BUFFERS       2
#define DEFAULT_MAX_BUFFERS       10

#define SAMPLECLIENT_COPY_NONSECURE_TO_SECURE 0

struct QSEECom_handle {
  unsigned char *ion_sbuffer;
};

long (*Content_Protection_Set_AppName)(const char*) = NULL;
long (*Content_Protection_Copy_Init)(struct QSEECom_handle**) = NULL;
long (*Content_Protection_Copy_Terminate)(struct QSEECom_handle**) = NULL;
long (*Content_Protection_Copy)(struct QSEECom_handle*, uint8_t*,
    const uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t) = NULL;

long res = 0;

std::unique_ptr<VmMem> mVmInst;
VmHandle mVmHandle;

static GstStaticPadTemplate gst_drm_decryptor_sink_pad_template =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_drm_decryptor_src_pad_template =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS_ANY);

enum {
  PROP_0,
  PROP_SESSION_ID,
  PROP_CDM_INSTANCE
};

static GstMemory *
gst_drm_decryptor_allocate_dma_buf (GstDrmDecryptor *decryptor,
    GstBuffer *in_buffer, gint size)
{
  GstMapInfo minfo;
  struct dma_heap_allocation_data alloc_data;
  gint result = 0, fd = -1;
  gchar *data;

  alloc_data.fd = 0;
  alloc_data.len = size;
  alloc_data.fd_flags = O_RDWR | O_CLOEXEC;
  alloc_data.heap_flags = 0;

  result = ioctl (decryptor->devfd, DMA_HEAP_IOCTL_ALLOC, &alloc_data);

  fd = alloc_data.fd;

  GST_DEBUG_OBJECT (decryptor, "Allocated DMA memory FD %d of size %d", fd, size);

  data = (char *)mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    close (fd);
    GST_ERROR_OBJECT (decryptor, "mmap failed for allocated dma memory");
    return NULL;
  }

  gst_buffer_map (in_buffer, &minfo, GST_MAP_READ);

  GST_DEBUG_OBJECT (decryptor, "copying content from input to output buffer size: %zu", minfo.maxsize);
  memcpy (data, minfo.data, minfo.maxsize);

  gst_buffer_unmap (in_buffer, &minfo);
  if (munmap (data, size))
    GST_ERROR_OBJECT (decryptor, "munmap failed !!");

  return gst_fd_allocator_alloc (decryptor->allocator, fd, size,
      GST_FD_MEMORY_FLAG_NONE);
}

static GstFlowReturn
gst_drm_decryptor_sinkpad_chain (GstPad *pad, GstObject *parent, GstBuffer *in_buffer)
{
  GstDrmDecryptor *decryptor = GST_DRM_DECRYPTOR (parent);
  GstBuffer *out_buffer = gst_buffer_new ();
  GstMemory *mem = NULL;
  GstMapInfo  inbuff_map_info;
  gsize max_size = 0;
  gsize inbuf_size = gst_buffer_get_sizes (in_buffer, NULL, &max_size);
  gint fd = -1;

  max_size = GST_ROUND_UP_N (max_size, 4096);

  mem = gst_drm_decryptor_allocate_dma_buf (decryptor, in_buffer, max_size);
  fd = gst_fd_memory_get_fd (mem);
  gst_buffer_append_memory (out_buffer, mem);

  gst_buffer_copy_into (out_buffer, in_buffer,
      (GstBufferCopyFlags) (GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS), 0, -1);


  res = mVmInst->LendDmabuf(fd, {{mVmHandle, VMMEM_READ | VMMEM_WRITE}});
  if (res == 0)
    GST_DEBUG_OBJECT (decryptor, "Lend Dma buf successful !");

  gst_buffer_unref (in_buffer);

  return gst_pad_push (decryptor->srcpad, out_buffer);
}

static void
gst_drm_decryptor_set_property (GObject *gobject, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
  GstDrmDecryptor *decryptor = GST_DRM_DECRYPTOR (gobject);

  switch (prop_id) {
    case PROP_SESSION_ID:
      g_free (decryptor->session_id);
      decryptor->session_id = g_strdup (g_value_get_string (value));
      break;
    case PROP_CDM_INSTANCE:
      decryptor->cdm_instance = g_value_get_pointer (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
  }
}

static void
gst_drm_decryptor_get_property (GObject *gobject, guint prop_id,
    GValue *value, GParamSpec *pspec)
{
  GstDrmDecryptor *decryptor = GST_DRM_DECRYPTOR (gobject);

  switch (prop_id) {
    case PROP_SESSION_ID:
      g_value_set_string (value, decryptor->session_id);
      break;
    case PROP_CDM_INSTANCE:
      g_value_set_pointer (value, decryptor->cdm_instance);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
  }
}

static void
gst_drm_decryptor_finalize (GObject *object)
{
  GstDrmDecryptor *decryptor = GST_DRM_DECRYPTOR (object);

  close (decryptor->devfd);
  g_object_unref (decryptor->allocator);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (decryptor));
}

static void
gst_drm_decryptor_init (GstDrmDecryptor *decryptor)
{
  decryptor->session_id = DEFAULT_PROP_SESSION_ID;
  decryptor->cdm_instance = NULL;
  decryptor->pool = NULL;

  decryptor->sinkpad = gst_pad_new_from_static_template (
    &gst_drm_decryptor_sink_pad_template, "sink");
  GST_PAD_SET_PROXY_CAPS (decryptor->sinkpad);

  decryptor->srcpad = gst_pad_new_from_static_template (
    &gst_drm_decryptor_src_pad_template, "src");
  GST_PAD_SET_PROXY_CAPS (decryptor->srcpad);

  gst_pad_set_chain_function (decryptor->sinkpad,
      GST_DEBUG_FUNCPTR (gst_drm_decryptor_sinkpad_chain));


  gst_element_add_pad (GST_ELEMENT (decryptor), decryptor->sinkpad);
  gst_element_add_pad (GST_ELEMENT (decryptor), decryptor->srcpad);

  decryptor->allocator = gst_fd_allocator_new ();
  decryptor->devfd = open ("/dev/dma_heap/qcom,system", O_RDONLY | O_CLOEXEC);

  mVmInst = VmMem::CreateVmMem();
  if (mVmInst) {
    mVmHandle = mVmInst->FindVmByName("qcom,cp_bitstream");
    if (mVmHandle < 0)  {
        GST_ERROR("Failed to find the qcom,cp_bitstream VM!\n");
    } else {
        GST_DEBUG("VmMem handle %x!", mVmHandle);
    }
  }

}

static void
gst_drm_decryptor_class_init (GstDrmDecryptorClass *klass)
{
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_drm_decryptor_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_drm_decryptor_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_drm_decryptor_finalize);

  gst_element_class_add_static_pad_template (element,
      &gst_drm_decryptor_sink_pad_template);
  gst_element_class_add_static_pad_template (element,
      &gst_drm_decryptor_src_pad_template);

  g_object_class_install_property (gobject, PROP_SESSION_ID,
      g_param_spec_string ("session-id", "Session ID",
          "Session id that is generated upon PR DRM plugin open session or WV DRM"
          " create session", DEFAULT_PROP_SESSION_ID, GParamFlags (
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_CDM_INSTANCE,
      g_param_spec_pointer ("cdm-instance", "CDM Instance",
          "Widevine CDM Instance to call CDM decrypt API",
          GParamFlags (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element,
      "QTI DRM Decryptor Plugin", GST_ELEMENT_FACTORY_KLASS_DECRYPTOR,
      "Uses Playready/Widevine DRM APIs to decrypt CENC scheme protected content",
      "QTI");

  GST_DEBUG_CATEGORY_INIT (decryptor_debug, "qtidrmdecryptor", 0,
      "QTI DRM Decryptor Plugin");
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "qtidrmdecryptor", GST_RANK_PRIMARY,
      GST_TYPE_DRM_DECRYPTOR);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtidrmdecryptor,
    "QTI DRM Decryptor Plugin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
