// Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef GST_PACKAGE_ORIGIN
#   define GST_PACKAGE_ORIGIN "-"
#endif

#include "gstqcodec2vdec.h"
#include "gstqcodec2venc.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_INFO ("qcodec2 plugin init");

  if (!gst_qcodec2_vdec_plugin_init (plugin)) {
    GST_ERROR ("qcodec2vdec plugin init error");
    return FALSE;
  }

  if (!gst_qcodec2_venc_plugin_init (plugin)) {
    GST_ERROR ("qcodec2venc plugin init error");
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    qcodec2, "GST QTI Codec2.0 Video Decoder & Encoder",
    plugin_init, VERSION, GST_LICENSE_UNKNOWN, PACKAGE_NAME, GST_PACKAGE_ORIGIN)
