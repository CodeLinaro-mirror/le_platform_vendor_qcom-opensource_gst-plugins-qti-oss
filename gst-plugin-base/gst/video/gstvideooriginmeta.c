/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "gstvideooriginmeta.h"

GType
gst_video_origin_meta_api_get_type (void)
{
  static GType gtype = 0;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&gtype)) {
    GType type = gst_meta_api_type_register ("GstVideoOriginMetaAPI",
        tags);
    g_once_init_leave (&gtype, type);
  }
  return gtype;
}

const GstMetaInfo *
gst_video_origin_meta_get_info (void)
{
  static const GstMetaInfo *minfo = NULL;

  if (g_once_init_enter (&minfo)) {
    const GstMetaInfo *info = gst_meta_register (
        GST_VIDEO_ORIGIN_META_API_TYPE, "GstVideoOriginMeta",
        sizeof (GstVideoOriginMeta), NULL, NULL, NULL);
    g_once_init_leave (&minfo, info);
  }
  return minfo;
}

GstVideoOriginMeta *
gst_buffer_add_video_origin_meta (GstBuffer * buffer)
{
  GstVideoOriginMeta *meta;

  meta = GST_VIDEO_ORIGIN_META_CAST (gst_buffer_add_meta (buffer,
      GST_VIDEO_ORIGIN_META_INFO, NULL));
  if (NULL == meta) {
    GST_ERROR ("Failed to add VTransform meta to buffer %p!", buffer);
    return NULL;
  }

  return meta;
}

GstVideoOriginMeta *
gst_buffer_get_video_origin_meta (GstBuffer * buffer)
{
  GstVideoOriginMeta *vframe_meta;

  vframe_meta = GST_VIDEO_ORIGIN_META_CAST (gst_buffer_get_meta (buffer,
      GST_VIDEO_ORIGIN_META_API_TYPE));

  return vframe_meta;
}
