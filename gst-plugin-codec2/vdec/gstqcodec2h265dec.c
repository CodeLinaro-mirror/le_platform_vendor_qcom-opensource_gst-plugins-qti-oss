/*
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

#include <gst/gst.h>
#include "gstqcodec2h265dec.h"

GST_DEBUG_CATEGORY_EXTERN (gst_qticodec2vdec_debug);
#define GST_CAT_DEFAULT gst_qticodec2vdec_debug

static gboolean gst_qcodec2_h265_dec_set_format (Gstqticodec2vdec * decoder,
    GstVideoCodecState * state);

/* class initialization */
G_DEFINE_TYPE (GstQcodec2H265Dec, gst_qcodec2_h265_dec, GST_TYPE_QTICODEC2VDEC);

static GstStaticPadTemplate gst_qcodec2_h265_dec_sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (H265_CAPS));

static void
gst_qcodec2_h265_dec_class_init (GstQcodec2H265DecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  Gstqticodec2vdecClass *qcodec2vdec_class = GST_QTICODEC2VDEC_CLASS (klass);

  qcodec2vdec_class->set_format = gst_qcodec2_h265_dec_set_format;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qcodec2_h265_dec_sink_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Codec2 video H.265 decoder", "Decoder/Video",
      "Video H.265 Decoder based on Codec2.0", "QTI");
}

static void
gst_qcodec2_h265_dec_init (GstQcodec2H265Dec * self)
{
}

static gboolean
gst_qcodec2_h265_dec_set_format (Gstqticodec2vdec * decoder,
    GstVideoCodecState * state)
{
  Gstqticodec2vdec *base_dec = decoder;
  GstQcodec2H265Dec *dec = GST_QCODEC2_H265_DEC (decoder);
  GstStructure *structure = NULL;
  GPtrArray *config = NULL;
  const gchar *profile_string = NULL;
  gboolean is_10bit = FALSE;
  GstVideoFormat output_format = GST_VIDEO_FORMAT_NV12;
  ConfigParams pixelformat;
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (dec, "H265 dec set format");

  structure = gst_caps_get_structure (state->caps, 0);
  profile_string = gst_structure_get_string (structure, "profile");
  if (!profile_string) {
    GST_DEBUG_OBJECT (dec, "no profile field in caps");
  } else {
    GST_DEBUG_OBJECT (dec, "profile:%s", profile_string);
    if (!g_strcmp0 (profile_string, "main-10")) {
      is_10bit = TRUE;
      GST_DEBUG_OBJECT (dec, "10bit output");
    }
  }

  if (is_10bit) {
    if (base_dec->is_ubwc)
      output_format = GST_VIDEO_FORMAT_NV12_10LE32;
    else
      output_format = GST_VIDEO_FORMAT_P010_10LE;
  }

  config = g_ptr_array_new ();
  if (config) {
    pixelformat =
        make_pixel_format_param (gst_to_c2_pixelformat (base_dec,
            output_format), FALSE);
    GST_LOG_OBJECT (dec, "set c2 output format: %d for H265",
        pixelformat.pixelFormat.fmt);
    g_ptr_array_add (config, &pixelformat);
    if (!c2componentInterface_config (base_dec->comp_intf,
            config, BLOCK_MODE_MAY_BLOCK)) {
      GST_ERROR_OBJECT (dec, "Failed to set config");
      ret = FALSE;
    }
    g_ptr_array_free (config, TRUE);
  }

  base_dec->output_format = output_format;

  return ret;
}
