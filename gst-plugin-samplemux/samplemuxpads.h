/*
* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __GST_SAMPLEMUX_PADS_H__
#define __GST_SAMPLEMUX_PADS_H__

#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>

G_BEGIN_DECLS

#define GST_TYPE_SAMPLEMUX_SECONDARY_PAD (gst_samplemux_secondary_pad_get_type())
#define GST_SAMPLEMUX_SECONDARY_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SAMPLEMUX_SECONDARY_PAD,\
      GstSampleMuxSecondaryPad))
#define GST_SAMPLEMUX_SECONDARY_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SAMPLEMUX_SECONDARY_PAD,\
      GstSampleMuxSecondaryPadClass))
#define GST_IS_SAMPLEMUX_SECONDARY_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SAMPLEMUX_SECONDARY_PAD))
#define GST_IS_SAMPLEMUX_SECONDARY_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SAMPLEMUX_SECONDARY_PAD))
#define GST_SAMPLEMUX_SECONDARY_PAD_CAST(obj) ((GstSampleMuxSecondaryPad *)(obj))

#define GST_TYPE_SAMPLEMUX_SINK_PAD (gst_samplemux_sink_pad_get_type())
#define GST_SAMPLEMUX_SINK_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SAMPLEMUX_SINK_PAD,\
      GstSampleMuxSinkPad))
#define GST_SAMPLEMUX_SINK_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SAMPLEMUX_SINK_PAD,\
      GstSampleMuxSinkPadClass))
#define GST_IS_SAMPLEMUX_SINK_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SAMPLEMUX_SINK_PAD))
#define GST_IS_SAMPLEMUX_SINK_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SAMPLEMUX_SINK_PAD))
#define GST_SAMPLEMUX_SINK_PAD_CAST(obj) ((GstSampleMuxSinkPad *)(obj))

#define GST_TYPE_SAMPLEMUX_SRC_PAD (gst_samplemux_src_pad_get_type())
#define GST_SAMPLEMUX_SRC_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SAMPLEMUX_SRC_PAD,\
      GstSampleMuxSrcPad))
#define GST_SAMPLEMUX_SRC_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SAMPLEMUX_SRC_PAD,\
      GstSampleMuxSrcPadClass))
#define GST_IS_SAMPLEMUX_SRC_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SAMPLEMUX_SRC_PAD))
#define GST_IS_SAMPLEMUX_SRC_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SAMPLEMUX_SRC_PAD))
#define GST_SAMPLEMUX_SRC_PAD_CAST(obj) ((GstSampleMuxSrcPad *)(obj))

#define GST_SAMPLEMUX_SRC_GET_LOCK(obj) (&GST_SAMPLEMUX_SRC_PAD(obj)->lock)
#define GST_SAMPLEMUX_SRC_LOCK(obj) \
    g_mutex_lock(GST_SAMPLEMUX_SRC_GET_LOCK(obj))
#define GST_SAMPLEMUX_SRC_UNLOCK(obj) \
    g_mutex_unlock(GST_SAMPLEMUX_SRC_GET_LOCK(obj))

#define GST_SAMPLEMUX_PAD_SIGNAL_IDLE(pad, idle) \
{\
  g_mutex_lock (&(pad->lock));                                     \
                                                                   \
  if (pad->is_idle != idle) {                                      \
    pad->is_idle = idle;                                           \
    GST_TRACE_OBJECT (pad, "State %s", idle ? "Idle" : "Running"); \
    g_cond_signal (&(pad->drained));                               \
  }                                                                \
                                                                   \
  g_mutex_unlock (&(pad->lock));                                   \
}

#define GST_SAMPLEMUX_PAD_WAIT_IDLE(pad) \
{\
  g_mutex_lock (&(pad->lock));                                         \
  GST_TRACE_OBJECT (pad, "Waiting until idle");                        \
                                                                       \
  while (!pad->is_idle) {                                              \
    gint64 endtime = g_get_monotonic_time () + 1 * G_TIME_SPAN_SECOND; \
                                                                       \
    if (!g_cond_wait_until (&(pad->drained), &(pad->lock), endtime))   \
      GST_WARNING_OBJECT (pad, "Timeout while waiting for idle!");     \
  }                                                                    \
                                                                       \
  GST_TRACE_OBJECT (pad, "Received idle");                             \
  g_mutex_unlock (&(pad->lock));                                       \
}

typedef struct _GstMetaItem GstMetaItem;

typedef struct _GstSampleMuxSecondaryPad GstSampleMuxSecondaryPad;
typedef struct _GstSampleMuxSecondaryPadClass GstSampleMuxSecondaryPadClass;

typedef struct _GstSampleMuxSinkPad GstSampleMuxSinkPad;
typedef struct _GstSampleMuxSinkPadClass GstSampleMuxSinkPadClass;

typedef struct _GstSampleMuxSrcPad GstSampleMuxSrcPad;
typedef struct _GstSampleMuxSrcPadClass GstSampleMuxSrcPadClass;

typedef enum {
  GST_SECONDARY_TYPE_UNKNOWN,
  GST_SECONDARY_TYPE_NV12,
  GST_SECONDARY_TYPE_RAW,
  GST_SECONDARY_TYPE_TEXT,
} GstSecondaryType;

struct _GstMetaItem {
  /// Parsed metadata in list format containing GstStructure.
  GList        *values;
  /// The timestamp corresponding to the metadata entry.
  GstClockTime timestamp;
};

struct _GstSampleMuxSecondaryPad {
  /// Inherited parent structure.
  GstPad            parent;

  // Format of negotiated metadata.
  GstSecondaryType  type;
  /// Segment.
  GstSegment        segment;

  /// Queue for managing parsed #GstMetaItem data.
  GQueue            *queue;
};

struct _GstSampleMuxSecondaryPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

struct _GstSampleMuxSinkPad {
  /// Inherited parent structure.
  GstPad       parent;

  /// Global mutex lock.
  GMutex       lock;

  /// Condition for signalling that last buffer was submitted downstream.
  GCond        drained;
  /// Flag indicating that there is no more work for processing.
  gboolean     is_idle;

  /// Queue for managing incoming video buffers.
  GstDataQueue *buffers;

  /// The count of buffers the queue can hold.
  guint        buffers_limit;
};

struct _GstSampleMuxSinkPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

struct _GstSampleMuxSrcPad {
  /// Inherited parent structure.
  GstPad       parent;

  /// Global mutex lock.
  GMutex       lock;

  /// Condition for signalling that last buffer was submitted downstream.
  GCond        drained;
  /// Flag indicating that there is no more work for processing.
  gboolean     is_idle;

  /// Segment.
  GstSegment   segment;

  /// Queue for output buffers.
  GstDataQueue *buffers;

  /// The count of buffers the queue can hold.
  guint        buffers_limit;
};

struct _GstSampleMuxSrcPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};


GType gst_samplemux_secondary_pad_get_type (void);

GType gst_samplemux_sink_pad_get_type (void);

GType gst_samplemux_src_pad_get_type (void);

gboolean gst_samplemux_src_pad_event (GstPad * pad, GstObject * parent,
                                    GstEvent * event);
gboolean gst_samplemux_src_pad_query (GstPad * pad, GstObject * parent,
                                    GstQuery * query);
gboolean gst_samplemux_src_pad_activate_mode (GstPad * pad, GstObject * parent,
                                            GstPadMode mode, gboolean active);

G_END_DECLS

#endif // __GST_SAMPLEMUX_PADS_H__
