/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __GST_SAMPLE_STITCHING_SINKPAD_H__
#define __GST_SAMPLE_STITCHING_SINKPAD_H__

#include <gst/gst.h>
#include <gst/base/gstaggregator.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_SAMPLE_STITCHING_SINKPAD \
  (gst_sample_stitching_sinkpad_get_type())
#define GST_SAMPLE_STITCHING_SINKPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SAMPLE_STITCHING_SINKPAD,\
                              GstSampleStitchingSinkPad))
#define GST_SAMPLE_STITCHING_SINKPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SAMPLE_STITCHING_SINKPAD,\
                           GstSampleStitchingSinkPadClass))
#define GST_IS_SAMPLE_STITCHING_SINKPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SAMPLE_STITCHING_SINKPAD))
#define GST_IS_SAMPLE_STITCHING_SINKPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SAMPLE_STITCHING_SINKPAD))
#define GST_SAMPLE_STITCHING_SINKPAD_CAST(obj) ((GstSampleStitchingSinkPad *)(obj))

#define GST_SAMPLE_STITCHING_SINKPAD_GET_LOCK(obj) \
  (&GST_SAMPLE_STITCHING_SINKPAD(obj)->lock)
#define GST_SAMPLE_STITCHING_SINKPAD_LOCK(obj) \
  g_mutex_lock(GST_SAMPLE_STITCHING_SINKPAD_GET_LOCK(obj))
#define GST_SAMPLE_STITCHING_SINKPAD_UNLOCK(obj) \
  g_mutex_unlock(GST_SAMPLE_STITCHING_SINKPAD_GET_LOCK(obj))

typedef struct _GstSampleStitchingSinkPad GstSampleStitchingSinkPad;
typedef struct _GstSampleStitchingSinkPadClass GstSampleStitchingSinkPadClass;

struct _GstSampleStitchingSinkPad {
  /// Inherited parent structure.
  GstAggregatorPad        parent;

  /// Global mutex lock.
  GMutex                  lock;

  /// Sink pad index.
  guint                   index;
  /// Negotiated caps on the pad input parsed to video info.
  GstVideoInfo            *info;

  /// Properties.
  GstVideoRectangle       destination;
};

struct _GstSampleStitchingSinkPadClass {
  /// Inherited parent structure.
  GstAggregatorPadClass parent;
};

GType gst_sample_stitching_sinkpad_get_type (void);

gboolean
gst_sample_stitching_sinkpad_setcaps (GstAggregatorPad * sinkpad,
                                      GstAggregator * aggregator,
                                      GstCaps * caps);

gboolean
gst_sample_stitching_sinkpad_acceptcaps (GstAggregatorPad * sinkpad,
                                         GstAggregator * aggregator,
                                         GstCaps * caps);

GstCaps *
gst_sample_stitching_sinkpad_getcaps (GstAggregatorPad * sinkpad,
                                      GstAggregator * aggregator,
                                      GstCaps * filter);

G_END_DECLS

#endif // __GST_SAMPLE_STITCHING_SINKPAD_H__
