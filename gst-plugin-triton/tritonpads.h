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

#ifndef __GST_TRITON_PADS_H__
#define __GST_TRITON_PADS_H__

#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>

G_BEGIN_DECLS

#define GST_TYPE_TRITON_SINKPAD (gst_triton_sinkpad_get_type())
#define GST_TRITON_SINKPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TRITON_SINKPAD,GstTritonSinkPad))
#define GST_TRITON_SINKPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TRITON_SINKPAD,GstTritonSinkPadClass))
#define GST_IS_TRITON_SINKPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TRITON_SINKPAD))
#define GST_IS_TRITON_SINKPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TRITON_SINKPAD))
#define GST_TRITON_SINKPAD_CAST(obj) ((GstTritonSinkPad *)(obj))

#define GST_TYPE_TRITON_SRCPAD (gst_triton_srcpad_get_type())
#define GST_TRITON_SRCPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TRITON_SRCPAD,GstTritonSrcPad))
#define GST_TRITON_SRCPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TRITON_SRCPAD,GstTritonSrcPadClass))
#define GST_IS_TRITON_SRCPAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TRITON_SRCPAD))
#define GST_IS_TRITON_SRCPAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TRITON_SRCPAD))
#define GST_TRITON_SRCPAD_CAST(obj) ((GstTritonSrcPad *)(obj))

typedef struct _GstTritonSinkPad GstTritonSinkPad;
typedef struct _GstTritonSinkPadClass GstTritonSinkPadClass;
typedef struct _GstTritonSrcPad GstTritonSrcPad;
typedef struct _GstTritonSrcPadClass GstTritonSrcPadClass;

struct _GstTritonSinkPad {
  /// Inherited parent structure.
  GstPad     parent;

  /// Segment.
  GstSegment segment;
};

struct _GstTritonSinkPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

struct _GstTritonSrcPad {
  /// Inherited parent structure.
  GstPad       parent;

  /// Segment.
  GstSegment   segment;

  /// Worker queue.
  GstDataQueue *buffers;

  /// Transmit plugin info.
  GstObject *object;

  /// Eos flag
  gboolean eos_flag;
  GstEvent * eos_event;

  /// Source pad status
  gboolean pushing_buffer;
};

struct _GstTritonSrcPadClass {
  /// Inherited parent structure.
  GstPadClass parent;
};

GType gst_triton_sinkpad_get_type (void);

GType gst_triton_srcpad_get_type (void);

G_END_DECLS

#endif // __GST_TRITON_PADS_H__
