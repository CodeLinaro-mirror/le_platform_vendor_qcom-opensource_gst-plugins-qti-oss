/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "drmdecryptor.h"

#include "gstmempool.h"

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
#define OUTPUT_BUF_SIZE_PROP_PREDEFINED 0
#define OUTPUT_BUF_SIZE_PROP_CALCULATE -1
#define OUTPUT_BUF_SIZE_PROP_DEFAULT OUTPUT_BUF_SIZE_PROP_PREDEFINED
#define MB_SIZE_IN_PIXEL          (16 * 16)
#define NUM_MBS_PER_FRAME(__width, __height) \
    (((__width + 15) >> 4) * ((__height + 15) >> 4))
#define NUM_MBS_4k NUM_MBS_PER_FRAME(4096, 2304)

static GstStaticPadTemplate gst_drm_decryptor_sink_pad_template =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("application/x-cenc, "
      "protection-system = (string) " PLAYREADY_SYSTEM_ID
#ifdef ENABLE_WIDEVINE
      ";"
      "application/x-cenc, "
      "protection-system = (string) " WIDEVINE_SYSTEM_ID
      ";"
      "application/x-webm-enc"
#endif
  )
);

static GstStaticPadTemplate gst_drm_decryptor_src_pad_template =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("video/x-h264;"
      "video/x-h265;"
      "video/x-vp8;"
      "video/x-vp9")
);

enum {
  PROP_0,
  PROP_SESSION_ID,
  PROP_CDM_INSTANCE,
  PROP_OUTPUT_BUF_SIZE,
};

static gboolean
gst_drm_decryptor_update_srccaps (GstDrmDecryptor *decryptor, GstCaps *caps)
{
  GstStructure *structure;
  GstCaps *src_caps, *updated_caps;
  const gchar *media_type;

  GST_INFO_OBJECT (decryptor, "Sink caps: %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);

  media_type = gst_structure_get_string (structure, "original-media-type");
  if (!media_type) {
    GST_ERROR_OBJECT (decryptor, "Original media type not found !");
    return FALSE;
  }

  structure = gst_structure_copy (structure);
  gst_structure_set_name (structure, media_type);
  gst_structure_remove_fields (structure, "original-media-type",
      "protection-system",
      NULL);

  src_caps = gst_pad_get_pad_template_caps (decryptor->srcpad);

  updated_caps = gst_caps_new_empty();
  gst_caps_append_structure (updated_caps, structure);

  if (gst_caps_can_intersect (updated_caps, src_caps)) {
    gst_pad_set_caps (decryptor->srcpad, updated_caps);
    GST_INFO_OBJECT (decryptor, "Src caps: %" GST_PTR_FORMAT, updated_caps);
  } else {
    GST_ERROR_OBJECT (decryptor, "No intersection between new caps and allowed caps");
    gst_caps_unref (updated_caps);
    gst_caps_unref (src_caps);
    return FALSE;
  }

  if (decryptor->original_media_type) {
    g_free (decryptor->original_media_type);
  }
  decryptor->original_media_type = g_strdup (media_type);

  gst_caps_unref (updated_caps);
  gst_caps_unref (src_caps);

  return TRUE;
}


static guint
calculate_output_buffer_size (GstDrmDecryptor *decryptor)
{
  guint frame_size;
  guint div_factor = 1;
  guint base_res_mbs = NUM_MBS_4k;

  if (g_strcmp0 (decryptor->original_media_type, "video/x-vp8") == 0 ||
      g_strcmp0 (decryptor->original_media_type, "video/x-vp9") == 0) {
    div_factor = 1;
  } else {
    div_factor = 2;
  }

  // for secure sessions, the required size is halved compared to the normal case.
  div_factor = div_factor << 1;

  frame_size = base_res_mbs * MB_SIZE_IN_PIXEL * 3 / 2 / div_factor;

  // multiply by 10/8 (1.25) to get size to cover possible 10 bit case h265 and vp9
  if (g_strcmp0 (decryptor->original_media_type, "video/x-h265") == 0 ||
      g_strcmp0 (decryptor->original_media_type, "video/x-vp9") == 0) {
    frame_size = frame_size + (frame_size >> 2);
  }

  return GST_ROUND_UP_N(frame_size, 4096);
}

static GstBufferPool*
gst_drm_decryptor_create_pool (GstDrmDecryptor *decryptor)
{
  GstStructure *config = NULL;
  GstBufferPool *pool = NULL;
  GstAllocator *allocator = NULL;
  guint buffer_size = 0;

  switch (decryptor->output_buf_size) {
      case OUTPUT_BUF_SIZE_PROP_DEFAULT:
        buffer_size = DEFAULT_BUFFER_SIZE;
        break;
      case OUTPUT_BUF_SIZE_PROP_CALCULATE:
        buffer_size = calculate_output_buffer_size (decryptor);
        GST_INFO_OBJECT (decryptor, "calculate output buffer size as %u",
            buffer_size);
        break;
      default:
        if (decryptor->output_buf_size < 0) {
          GST_ERROR_OBJECT (decryptor, "Invalid output buffer size: %d",
              decryptor->output_buf_size);
          return NULL;
        }
        buffer_size = decryptor->output_buf_size;
        break;
  }

  if (!(pool = gst_mem_buffer_pool_new (GST_MEMORY_BUFFER_POOL_TYPE_SECURE))) {
    GST_ERROR_OBJECT (decryptor, "Failed to create new buffer pool !");
    return NULL;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, NULL, buffer_size,
      DEFAULT_MIN_BUFFERS, DEFAULT_MAX_BUFFERS);

  if (!(allocator = gst_dmabuf_allocator_new ())) {
    GST_ERROR_OBJECT (decryptor, "Failed to create dmabuf allocator !");
    g_clear_object (&pool);
    return NULL;
  }
  gst_buffer_pool_config_set_allocator (config, allocator, NULL);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (decryptor, "Failed to set pool configuration !");
    g_clear_object (&pool);
  }

  g_object_unref (allocator);
  return pool;
}

static GstFlowReturn
gst_drm_decryptor_sinkpad_chain (GstPad *pad, GstObject *parent, GstBuffer *in_buffer)
{
  GstDrmDecryptor *decryptor = GST_DRM_DECRYPTOR (parent);
  GstBuffer *out_buffer = NULL;
  GstFlowReturn result = GST_FLOW_OK;

  // TODO: Video backend is failing to handle vp9 clear content on secure path.
  // Added this temporary check to skip clear content until the issue is fixed.
  GstProtectionMeta *pmeta = gst_buffer_get_protection_meta (in_buffer);
  if (pmeta == NULL) {
    GstCaps *caps = gst_pad_get_current_caps (decryptor->srcpad);
    const gchar *name = gst_structure_get_name (
        gst_caps_get_structure (caps, 0));
    gst_caps_unref (caps);
    if (g_str_equal (name, "video/x-vp9")) {
      GST_WARNING_OBJECT (decryptor, "No protection metadata found for vp9 "
      "content. Dropping buffer !");
      gst_buffer_unref (in_buffer);
      return GST_FLOW_OK;
    }
  }

  result = gst_buffer_pool_acquire_buffer (decryptor->pool, &out_buffer, NULL);
  if (result != GST_FLOW_OK) {
    if (result == GST_FLOW_FLUSHING) {
      GST_INFO_OBJECT (decryptor, "Failed to acquire secure buffer from pool, we are flushing");
      return result;
    } else {
      GST_ERROR_OBJECT (decryptor, "Failed to acquire secure buffer from pool, result %d", result);
      return GST_FLOW_ERROR;
    }
  }

  if (gst_drm_decryptor_engine_execute (decryptor->engine, in_buffer,
      out_buffer) != 0) {
    gst_buffer_unref (out_buffer);
    gst_buffer_unref (in_buffer);
    return GST_FLOW_OK;
  }

  GST_DEBUG_OBJECT (decryptor, "Decryption successful !");

  gst_buffer_copy_into (out_buffer, in_buffer,
      GstBufferCopyFlags (GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS), 0, -1);

  return gst_pad_push (decryptor->srcpad, out_buffer);
}

static gboolean
gst_drm_decryptor_sinkpad_event (GstPad *pad, GstObject *parent, GstEvent *event)
{
  GstDrmDecryptor *decryptor = GST_DRM_DECRYPTOR (parent);
  gboolean success = TRUE;

  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;

      gst_event_parse_caps (event, &caps);
      success = gst_drm_decryptor_update_srccaps (decryptor, caps);
      gst_event_unref (event);

      if (success && !decryptor->pool &&
          !(decryptor->pool = gst_drm_decryptor_create_pool (decryptor))) {
        GST_ERROR_OBJECT (decryptor, "Failed to create buffer pool!");
        return FALSE;
      }

      if (success && !gst_buffer_pool_is_active (decryptor->pool) &&
          !gst_buffer_pool_set_active (decryptor->pool, TRUE)) {
        GST_ERROR_OBJECT (decryptor, "Failed to activate buffer pool!");
        return FALSE;
      }
      break;
    }
    case GST_EVENT_PROTECTION:
    {
      const gchar *system_id;

      gst_event_parse_protection (event, &system_id, NULL, NULL);
      gst_event_unref (event);

      decryptor->engine = gst_drm_decryptor_engine_new (system_id,
          (gpointer) decryptor->session_id, decryptor->cdm_instance);
      g_return_val_if_fail (decryptor->engine != NULL, FALSE);
      break;
    }
    case GST_EVENT_FLUSH_START:
      if (decryptor->pool && gst_buffer_pool_is_active (decryptor->pool)) {
        GST_LOG_OBJECT (decryptor, "Setting bufferpool to flushing");
        gst_buffer_pool_set_flushing (decryptor->pool, TRUE);
        GST_LOG_OBJECT (decryptor, "Bufferpool flushed");
      }
      success = gst_pad_event_default (pad, parent, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      if (decryptor->pool && gst_buffer_pool_is_active (decryptor->pool)) {
        GST_LOG_OBJECT (decryptor, "Setting bufferpool to not flushing");
        gst_buffer_pool_set_flushing (decryptor->pool, FALSE);
        GST_LOG_OBJECT (decryptor, "Bufferpool not flushed");
      }
      success = gst_pad_event_default (pad, parent, event);
      break;
    default:
      success = gst_pad_event_default (pad, parent, event);
      break;
  }

  return success;
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
    case PROP_OUTPUT_BUF_SIZE:
      decryptor->output_buf_size = g_value_get_int (value);
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
    case PROP_OUTPUT_BUF_SIZE:
      g_value_set_int (value, decryptor->output_buf_size);
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

  if (decryptor->engine != NULL)
    delete decryptor->engine;

  if (decryptor->pool)
    gst_object_unref (decryptor->pool);

  if (decryptor->original_media_type) {
    g_free (decryptor->original_media_type);
    decryptor->original_media_type = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (decryptor));
}

static void
gst_drm_decryptor_init (GstDrmDecryptor *decryptor)
{
  decryptor->engine = NULL;
  decryptor->session_id = DEFAULT_PROP_SESSION_ID;
  decryptor->cdm_instance = NULL;
  decryptor->pool = NULL;
  decryptor->output_buf_size = OUTPUT_BUF_SIZE_PROP_DEFAULT;
  decryptor->original_media_type = NULL;

  decryptor->sinkpad = gst_pad_new_from_static_template (
      &gst_drm_decryptor_sink_pad_template, "sink");

  decryptor->srcpad = gst_pad_new_from_static_template (
      &gst_drm_decryptor_src_pad_template, "src");

  gst_pad_set_chain_function (decryptor->sinkpad,
      GST_DEBUG_FUNCPTR (gst_drm_decryptor_sinkpad_chain));
  gst_pad_set_event_function (decryptor->sinkpad,
      GST_DEBUG_FUNCPTR (gst_drm_decryptor_sinkpad_event));

  gst_element_add_pad (GST_ELEMENT (decryptor), decryptor->sinkpad);
  gst_element_add_pad (GST_ELEMENT (decryptor), decryptor->srcpad);
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

  g_object_class_install_property (gobject, PROP_OUTPUT_BUF_SIZE,
      g_param_spec_int ("output-buf-size", "Output Buffer Size",
          "Set the output buffer size. A value of 0 means to use the predefined "
          "buffer size(1024*1024*2). While -1 means to calculate the size based "
          "on the decoding bitstream buffer requirements of the Gen3 kernel driver, "
          "accounting for different codecs. Positive values specify a fixed buffer "
          "size. The buffer size value should follow alignment request of secure memory",
          G_MININT, G_MAXINT, OUTPUT_BUF_SIZE_PROP_DEFAULT,
          GParamFlags (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY)));

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
    PACKAGE_VERSION "-" G_STRINGIFY(GST_VERSION_MAJOR) "/" G_STRINGIFY(GST_VERSION_MINOR) "/" G_STRINGIFY(GST_VERSION_MICRO),
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)
