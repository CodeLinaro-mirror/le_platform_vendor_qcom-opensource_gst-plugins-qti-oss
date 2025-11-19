/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __GST_QTI_SAMPLEMUX_H__
#define __GST_QTI_SAMPLEMUX_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

#include "samplemuxpads.h"

G_BEGIN_DECLS

#define GST_TYPE_SAMPLEMUX (gst_samplemux_get_type())
#define GST_SAMPLEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SAMPLEMUX,GstSampleMux))
#define GST_SAMPLEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SAMPLEMUX,GstSampleMuxClass))
#define GST_IS_SAMPLEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SAMPLEMUX))
#define GST_IS_SAMPLEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SAMPLEMUX))
#define GST_SAMPLEMUX_CAST(obj)       ((GstSampleMux *)(obj))

#define GST_SAMPLEMUX_GET_LOCK(obj)   (&GST_SAMPLEMUX(obj)->lock)
#define GST_SAMPLEMUX_LOCK(obj)       g_mutex_lock(GST_SAMPLEMUX_GET_LOCK(obj))
#define GST_SAMPLEMUX_UNLOCK(obj)     g_mutex_unlock(GST_SAMPLEMUX_GET_LOCK(obj))

typedef struct _GstSampleMux GstSampleMux;
typedef struct _GstSampleMuxClass GstSampleMuxClass;

struct _GstSampleMux
{
  /// Inherited parent structure.
  GstElement            parent;

  /// Global mutex lock.
  GMutex                lock;

  /// Next available index for the sink pads.
  guint                 nextidx;

  /// Convenient local reference to data sink pads.
  GList                 *secondarypads;
  /// Convenient local reference to media sink pad.
  GstSampleMuxSinkPad   *sinkpad;
  /// Convenient local reference to source pad.
  GstSampleMuxSrcPad    *srcpad;

  /// Info regarding the negotiated video caps.
  GstVideoInfo          *vinfo;

  /// Worker task.
  GstTask               *worktask;
  /// Worker task mutex.
  GRecMutex             worklock;
  // Indicates whether the worker task is active or not.
  gboolean              active;
  /// Condition for push/pop buffers from the queues.
  GCond                 wakeup;
  /// The timestamp until which the worker task will wait for synced data.
  gint64                timeout;

  guint                 queue_size;
};

struct _GstSampleMuxClass {
  /// Inherited parent structure.
  GstElementClass parent;
};

GType gst_samplemux_get_type (void);

G_END_DECLS

#endif // __GST_QTI_SAMPLEMUX_H__
