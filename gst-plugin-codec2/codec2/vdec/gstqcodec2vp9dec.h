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

#ifndef __GST_QCODEC2_VP9_DEC_H__
#define __GST_QCODEC2_VP9_DEC_H__

#include <gst/gst.h>
#include "gstqcodec2vdec.h"
#define GST_USE_UNSTABLE_API
#include <gst/codecparsers/gstvp9parser.h>

G_BEGIN_DECLS
#define GST_TYPE_QCODEC2_VP9_DEC \
  (gst_qcodec2_vp9_dec_get_type())
#define GST_QCODEC2_VP9_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QCODEC2_VP9_DEC,GstQcodec2VP9Dec))
#define GST_QCODEC2_VP9_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QCODEC2_VP9_DEC,GstQcodec2VP9DecClass))
#define GST_QCODEC2_VP9_DEC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_QCODEC2_VP9_DEC,GstQcodec2VP9DecClass))
#define GST_IS_QCODEC2_VP9_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QCODEC2_VP9_DEC))
#define GST_IS_QCODEC2_VP9_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QCODEC2_VP9_DEC))
typedef struct _GstQcodec2VP9Dec GstQcodec2VP9Dec;
typedef struct _GstQcodec2VP9DecClass GstQcodec2VP9DecClass;

struct _GstQcodec2VP9Dec
{
  GstQcodec2Vdec parent;
  gboolean check_vp9_10bit;
};

struct _GstQcodec2VP9DecClass
{
  GstQcodec2VdecClass parent_class;
};

GType gst_qcodec2_vp9_dec_get_type (void);

G_END_DECLS
#endif /* __GST_QCODEC2_VP9_DEC_H__ */
