/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __GST_QTI_SAMPLE_STITCHING_H__
#define __GST_QTI_SAMPLE_STITCHING_H__

#include <gst/gst.h>
#include <gst/base/gstaggregator.h>
#include <gst/base/gstdataqueue.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_SAMPLE_STITCHING \
  (gst_sample_stitching_get_type())
#define GST_SAMPLE_STITCHING(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SAMPLE_STITCHING,GstSampleStitching))
#define GST_SAMPLE_STITCHING_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SAMPLE_STITCHING,GstSampleStitchingClass))
#define GST_IS_SAMPLE_STITCHING(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SAMPLE_STITCHING))
#define GST_IS_SAMPLE_STITCHING_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SAMPLE_STITCHING))
#define GST_SAMPLE_STITCHING_CAST(obj)       ((GstSampleStitching *)(obj))

#define GST_SAMPLE_STITCHING_GET_LOCK(obj) (&GST_SAMPLE_STITCHING(obj)->lock)
#define GST_SAMPLE_STITCHING_LOCK(obj) \
  g_mutex_lock(GST_SAMPLE_STITCHING_GET_LOCK(obj))
#define GST_SAMPLE_STITCHING_UNLOCK(obj) \
  g_mutex_unlock(GST_SAMPLE_STITCHING_GET_LOCK(obj))

#define GST_SS_VCE_COMPOSITION_INIT  { NULL, 0, NULL}

typedef struct _GstSSVideoBlit GstSSVideoBlit;
typedef struct _GstSSVideoComposition GstSSVideoComposition;
typedef struct _GstSampleStitching GstSampleStitching;
typedef struct _GstSampleStitchingClass GstSampleStitchingClass;

typedef enum {
  GST_SAMPLE_STITCHING_HORIZONTAL,
  GST_SAMPLE_STITCHING_VERTICAL,
} GSTSampleStitchingMode;

struct _GstSSVideoBlit
{
  GstVideoFrame      *frame;

  GstVideoRectangle  *sources;
  GstVideoRectangle  *destinations;
};

struct _GstSSVideoComposition
{
  GstSSVideoBlit    *blits;
  guint             n_blits;

  GstVideoFrame     *frame;
};

struct _GstSampleStitching {
  GstAggregator           parent;

  /// Global mutex lock.
  GMutex                  lock;

  /// Number of sink pads.
  guint                   n_inputs;

  /// Output pad caps.
  GstVideoInfo            *outinfo;
  /// Output buffer pool.
  GstBufferPool           *outpool;

  /// Output buffer duration.
  GstClockTime            duration;

  /// Worker task.
  GstTask                 *worktask;
  /// Worker task mutex.
  GRecMutex               worklock;
  /// Worker queue.
  GstDataQueue            *requests;

  GSTSampleStitchingMode  mode;
};

struct _GstSampleStitchingClass {
  GstAggregatorClass parent;
};

G_GNUC_INTERNAL GType gst_sample_stitching_get_type (void);

G_END_DECLS

#endif // __GST_QTI_SAMPLE_STITCHING_H__
