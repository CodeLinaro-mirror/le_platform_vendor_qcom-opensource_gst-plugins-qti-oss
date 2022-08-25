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

#ifndef __GST_QTI_TRITON_H__
#define __GST_QTI_TRITON_H__

#include <gst/gst.h>
#include <gst/base/gstdataqueue.h>

G_BEGIN_DECLS

#define GST_TYPE_TRITON (gst_triton_get_type())
#define GST_TRITON(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TRITON,GstTriton))
#define GST_TRITON_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TRITON,GstTritonClass))
#define GST_IS_TRITON(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TRITON))
#define GST_IS_TRITON_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TRITON))

#define GST_TRITON_GET_LOCK(obj)   (&GST_TRITON(obj)->lock)
#define GST_TRITON_LOCK(obj)       g_mutex_lock(GST_TRITON_GET_LOCK(obj))
#define GST_TRITON_UNLOCK(obj)     g_mutex_unlock(GST_TRITON_GET_LOCK(obj))
#define GST_TRITON_MODE (gst_triton_mode_get_type())
#define GST_TRITON_TASK (gst_triton_task_get_type())
#define GST_TYPE_TRITON_REQUEST  (gst_triton_request_get_type())
#define GST_TRITON_REQUESTS_COND(obj)   (&GST_TRITON(obj)->wakeup)
#define GST_TRITON_REQUEST(obj) ((GstTritonRequest *) obj)


typedef struct _GstTriton GstTriton;
typedef struct _GstTritonClass GstTritonClass;
typedef struct _GstTritonRequest GstTritonRequest;
typedef struct _InputBuf InputBuf;

typedef enum {
  HTTP_MODE,
  GRPC_MODE,
  C_API_MODE,
} GstTritonMode;

typedef enum {
  DETECTION,
  CLASSIFICATION,
  SEGMENTATION,
} GstTritonTask;

struct _GstTritonRequest {
  GstMiniObject parent;
  gboolean done;
  guint id;
  gpointer result;
  GList *outputs;
  GstBuffer *inbuffer;
};

struct _InputBuf
{
  gpointer buf;
  guint    size;
};

struct _GstTriton
{
  /// Inherited parent structure.
  GstElement parent;

  /// Global mutex lock.
  GMutex     lock;
  GCond      wakeup;

  /// Next available index for the source pads.
  guint      nextidx;

  /// Convenient local reference to sink pad.
  GstPad     *sinkpad;
  /// Convenient local reference to source pads.
  GList      *srcpads;
  /// Worker task.
  GstTask    *worktask;
  /// Worker task mutex.
  GRecMutex   worklock;
  // Indicates whether the worker task is active or not.
  gboolean    active;

  InputBuf   *input_buf;
  guint      block_size;
  gint       src_height;
  gint       src_width;

  GstDataQueue *requests;
  GstDataQueueSize *queue_size;
  GCond      queue_is_empty;

  /// Triton properties
  GstTritonMode infer_mode;
  GstTritonTask task;
  gchar      *url;
  gchar      *model_name;
  gchar      *model_version;
  gchar      *labels;
  gdouble    threshold;
  gboolean   keep_ratio;
  gpointer   client;
  gpointer   model_info;
};

struct _GstTritonClass {
  /// Inherited parent structure.
  GstElementClass parent;
};

GType gst_triton_get_type (void);
GType gst_triton_request_get_type(void);

G_END_DECLS

#endif // __GST_QTI_TRITON_H__
