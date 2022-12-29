/*
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
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

#include "mlaicpads.h"

G_DEFINE_TYPE(GstMLAicSinkPad, gst_ml_aic_sinkpad, GST_TYPE_PAD);
G_DEFINE_TYPE(GstMLAicSrcPad, gst_ml_aic_srcpad, GST_TYPE_PAD);

GST_DEBUG_CATEGORY_STATIC (gst_ml_aic_debug);
#define GST_CAT_DEFAULT gst_ml_aic_debug

static gboolean
queue_is_full_cb (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  // There won't be any condition limiting for the buffer queue size.
  return FALSE;
}

static void
gst_ml_aic_sinkpad_finalize (GObject * object)
{
  GstMLAicSinkPad *pad = GST_ML_AIC_SINKPAD (object);
#if MLAIC_PERF_DEBUG
  GstClockTime average_latency, average_process, average_wait;
#endif

  if (pad->pool != NULL) {
    gst_buffer_pool_set_active (pad->pool, FALSE);
    gst_object_unref (pad->pool);
  }

#if MLAIC_PERF_DEBUG
  if (pad->count) {
    average_latency = pad->time_latency / pad->count;
    average_process = pad->time_process / pad->count;
    average_wait = average_latency - average_process;
    GST_INFO_OBJECT (GST_PAD (pad), "SINKPAD average "
    "latency = %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " ms,"
    "process = %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " ms,"
    "wait = %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " ms,",
    GST_TIME_AS_MSECONDS (average_latency), (GST_TIME_AS_USECONDS (average_latency) % 1000),
    GST_TIME_AS_MSECONDS (average_process), (GST_TIME_AS_USECONDS (average_process) % 1000),
    GST_TIME_AS_MSECONDS (average_wait), (GST_TIME_AS_USECONDS (average_wait) % 1000));
  }
#endif

  g_mutex_clear (&pad->lock);

  G_OBJECT_CLASS (gst_ml_aic_sinkpad_parent_class)->finalize(object);
}

void
gst_ml_aic_sinkpad_class_init (GstMLAicSinkPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_aic_sinkpad_finalize);

  GST_DEBUG_CATEGORY_INIT (gst_ml_aic_debug, "qtimlaic", 0,
      "QTI ML AIC sink pad");
}

void
gst_ml_aic_sinkpad_init (GstMLAicSinkPad * pad)
{
  g_mutex_init (&pad->lock);
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);

  pad->pool = NULL;
#if MLAIC_PERF_DEBUG
  pad->timestamp = GST_CLOCK_TIME_NONE;
  pad->time_latency = 0;
  pad->time_process = 0;
  pad->count = 0;
#endif
}

static void
gst_ml_aic_srcpad_finalize (GObject * object)
{
  GstMLAicSrcPad *pad = GST_ML_AIC_SRCPAD (object);
#if MLAIC_PERF_DEBUG
  GstClockTime average_latency, average_process, average_wait, average_queue;
#endif

  gst_data_queue_set_flushing (pad->requests, TRUE);
  gst_data_queue_flush (pad->requests);

  gst_object_unref (GST_OBJECT_CAST(pad->requests));
  g_cond_clear (&pad->cond);
  g_mutex_clear (&pad->lock);
  pad->eos = TRUE;
  pad->in_use = FALSE;

#if MLAIC_PERF_DEBUG
  if (pad->count) {
    average_latency = pad->time_latency / pad->count;
    average_process = pad->time_process / pad->count;
    average_queue = pad->time_queue / pad->count;
    average_wait = average_latency - average_process;
    GST_INFO_OBJECT (GST_PAD (pad), "SRCPAD average "
    "latency = %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " ms,"
    "process = %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " ms,"
    "queue = %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " ms,"
    "wait = %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " ms,",
    GST_TIME_AS_MSECONDS (average_latency), (GST_TIME_AS_USECONDS (average_latency) % 1000),
    GST_TIME_AS_MSECONDS (average_process), (GST_TIME_AS_USECONDS (average_process) % 1000),
    GST_TIME_AS_MSECONDS (average_queue), (GST_TIME_AS_USECONDS (average_queue) % 1000),
    GST_TIME_AS_MSECONDS (average_wait), (GST_TIME_AS_USECONDS (average_wait) % 1000));
  }
#endif
  G_OBJECT_CLASS (gst_ml_aic_srcpad_parent_class)->finalize(object);
}

void
gst_ml_aic_srcpad_class_init (GstMLAicSrcPadClass * klass)
{
  GObjectClass *gobject = (GObjectClass *) klass;

  gobject->finalize = GST_DEBUG_FUNCPTR (gst_ml_aic_srcpad_finalize);

  GST_DEBUG_CATEGORY_INIT (gst_ml_aic_debug, "qtimlaic", 0,
      "QTI ML AIC src pad");
}

void
gst_ml_aic_srcpad_init (GstMLAicSrcPad * pad)
{
  pad->requests = gst_data_queue_new (queue_is_full_cb, NULL, NULL, NULL);
  pad->in_use = FALSE;
  pad->eos = FALSE;
  g_mutex_init (&pad->lock);
  g_cond_init (&pad->cond);
#if MLAIC_PERF_DEBUG
  pad->timestamp = GST_CLOCK_TIME_NONE;
  pad->time_latency = 0;
  pad->time_process = 0;
  pad->time_queue = 0;
  pad->count = 0;
#endif
}

