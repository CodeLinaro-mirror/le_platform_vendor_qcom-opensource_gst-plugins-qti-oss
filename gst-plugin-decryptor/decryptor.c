/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "decryptor.h"

#define GST_CAT_DEFAULT decryptor_debug
GST_DEBUG_CATEGORY_STATIC (decryptor_debug);

#define gst_decryptor_parent_class parent_class
G_DEFINE_TYPE (GstDecryptor, gst_decryptor, GST_TYPE_ELEMENT);

#define DEFAULT_PROP_SESSION_ID   NULL
#define KEY_SIZE                  16
#define PLAYREADY_SYSTEM_ID       "9a04f079-9840-4286-ab92-e65be0885f95"

static GstStaticPadTemplate gst_decryptor_sink_pad_template =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("application/x-cenc, "
      "protection-system = (string) "PLAYREADY_SYSTEM_ID)
);

static GstStaticPadTemplate gst_decryptor_src_pad_template =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("ANY")
);

enum {
  PROP_0,
  PROP_SESSION_ID
};

static gboolean
gst_decryptor_update_srccaps (GstDecryptor *decryptor, GstCaps *caps) {
  GstStructure *structure, *src_structure;
  GstCaps *srccaps;
  const gchar *media_type;

  structure = gst_caps_get_structure (caps, 0);
  src_structure = gst_structure_copy (structure);

  media_type = gst_structure_get_string (src_structure, "original-media-type");
  gst_structure_set_name (src_structure, media_type);
  gst_structure_remove_fields (src_structure, "original-media-type",
        "protection-system",
        NULL);

  srccaps = gst_caps_new_empty();
  gst_caps_append_structure (srccaps, src_structure);
  gst_pad_set_caps (decryptor->srcpad, srccaps);

  GST_INFO_OBJECT (decryptor, "updated src caps: %" GST_PTR_FORMAT, srccaps);

  gst_caps_unref (srccaps);

  return TRUE;
}

static GstFlowReturn
gst_decryptor_sinkpad_chain (GstPad *pad, GstObject *parent, GstBuffer *in_buffer)
{
  GstDecryptor *decryptor = GST_DECRYPTOR (parent);
  GstBuffer *out_buffer = NULL;

  GstProtectionMeta *encr_meta = gst_buffer_get_protection_meta (in_buffer);
  if (encr_meta == NULL)
  {
    GST_ERROR_OBJECT (decryptor, "No protection metadata in buffer !");
    return GST_FLOW_ERROR;
  }

  if (!gst_decryptor_context_execute (decryptor->context, in_buffer, &out_buffer))
  {
    GST_ERROR_OBJECT (decryptor, "Decryption failed !");
    return GST_FLOW_OK;
  }

  gst_buffer_copy_into (out_buffer, in_buffer,
                    GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  return gst_pad_push (decryptor->srcpad, out_buffer);
}

static gboolean
gst_decryptor_sinkpad_event (GstPad *pad, GstObject *parent, GstEvent *event) {
  GstDecryptor *decryptor = GST_DECRYPTOR (parent);
  gboolean result = FALSE;

  switch (GST_EVENT_TYPE(event))
  {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps = NULL;

      gst_event_parse_caps (event, &caps);
      result = gst_decryptor_update_srccaps (decryptor, caps);
      gst_event_unref (event);

      break;
    }
    default:
      result = gst_pad_event_default (pad, parent, event);
      break;
  }

  return result;
}

static void
gst_decryptor_set_property (GObject *gobject, guint prop_id,
    const GValue *value, GParamSpec *pspec) {
  GstDecryptor *decryptor = GST_DECRYPTOR (gobject);

  switch (prop_id) {
    case PROP_SESSION_ID:
      g_free (decryptor->session_id);
      decryptor->session_id = g_strdup (g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
  }
}

static void
gst_decryptor_get_property (GObject *gobject, guint prop_id,
    GValue *value, GParamSpec *pspec) {
  GstDecryptor *decryptor = GST_DECRYPTOR (gobject);

  switch (prop_id) {
    case PROP_SESSION_ID:
      g_value_set_string (value, decryptor->session_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (gobject, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_decryptor_change_state (GstElement *element, GstStateChange transition)
{
  GstDecryptor *decryptor = GST_DECRYPTOR (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
      decryptor->context = gst_decryptor_context_new (decryptor->session_id);
      if (decryptor->context == NULL)
      {
        GST_ERROR_OBJECT (decryptor, "Decryptor context initialization failed!");
        ret = GST_STATE_CHANGE_FAILURE;
        return ret;
      }

      break;
    }

    default:
    break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
      if (decryptor->context != NULL)
      {
        gst_decryptor_context_free (decryptor->context);
        decryptor->context = NULL;
      }

      break;
    }

    default:
    break;
  }

  return ret;
}

static void
gst_decryptor_finalize (GObject *object)
{
  GstDecryptor *decryptor = GST_DECRYPTOR (object);
  g_free (decryptor->session_id);
  if (decryptor->context != NULL)
    gst_decryptor_context_free (decryptor->context);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (decryptor));
}

static void
gst_decryptor_init (GstDecryptor *decryptor) {

  decryptor->context = NULL;
  decryptor->session_id = DEFAULT_PROP_SESSION_ID;

  decryptor->sinkpad = gst_pad_new_from_static_template (
    &gst_decryptor_sink_pad_template, "sink");

  decryptor->srcpad = gst_pad_new_from_static_template (
    &gst_decryptor_src_pad_template, "src");

  gst_pad_set_chain_function (decryptor->sinkpad,
    GST_DEBUG_FUNCPTR (gst_decryptor_sinkpad_chain));
  gst_pad_set_event_function (decryptor->sinkpad,
    GST_DEBUG_FUNCPTR (gst_decryptor_sinkpad_event));

  gst_element_add_pad (GST_ELEMENT (decryptor), decryptor->sinkpad);
  gst_element_add_pad (GST_ELEMENT (decryptor), decryptor->srcpad);
}

static void
gst_decryptor_class_init (GstDecryptorClass *klass) {
  GObjectClass *gobject = G_OBJECT_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_decryptor_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_decryptor_get_property);
  gobject->finalize = GST_DEBUG_FUNCPTR (gst_decryptor_finalize);

  gst_element_class_add_static_pad_template (element,
    &gst_decryptor_sink_pad_template);
  gst_element_class_add_static_pad_template (element,
    &gst_decryptor_src_pad_template);

  g_object_class_install_property (gobject, PROP_SESSION_ID,
      g_param_spec_string ("session-id", "Session-ID",
          "DRM Session ID", DEFAULT_PROP_SESSION_ID,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element->change_state = GST_DEBUG_FUNCPTR (gst_decryptor_change_state);

  gst_element_class_set_static_metadata (element,
  "QTI Decryptor Plugin", GST_ELEMENT_FACTORY_KLASS_DECRYPTOR,
  "Uses Playready DRM APIs to decrypt protected content",
  "QTI");

  GST_DEBUG_CATEGORY_INIT (decryptor_debug, "qtidecryptor", 0,
    "QTI Decryptor Plugin");
}

static gboolean
plugin_init (GstPlugin *plugin) {

  return gst_element_register (plugin, "qtidecryptor", GST_RANK_PRIMARY,
    GST_TYPE_DECRYPTOR);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  qtidecryptor,
  "QTI Decryptor Plugin",
  plugin_init,
  PACKAGE_VERSION,
  PACKAGE_LICENSE,
  PACKAGE_SUMMARY,
  PACKAGE_ORIGIN
)
