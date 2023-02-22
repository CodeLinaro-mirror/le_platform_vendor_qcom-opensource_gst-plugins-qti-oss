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


#ifndef __GST_QCODEC2BUFFERPOOL_H__
#define __GST_QCODEC2BUFFERPOOL_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>
#include <gst/video/gstvideopool.h>
#include <gst/allocators/allocators.h>
#include <gst/allocators/gstdmabuf.h>

G_BEGIN_DECLS
/* buffer pool functions */
#define GST_TYPE_QCODEC2_BUFFER_POOL      (gst_qcodec2_buffer_pool_get_type())
#define GST_IS_QCODEC2_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QCODEC2_BUFFER_POOL))
#define GST_QCODEC2_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QCODEC2_BUFFER_POOL, GstQcodec2BufferPool))
#define GST_QCODEC2_BUFFER_POOL_CAST(obj) ((GstQcodec2BufferPool*)(obj))
typedef struct _GstQcodec2BufferPool GstQcodec2BufferPool;
typedef struct _GstQcodec2BufferPoolClass GstQcodec2BufferPoolClass;
typedef struct _GstBufferPoolInitParam GstBufferPoolInitParam;

/* Gst C2 Comp functions */
#define GST_TYPE_C2_COMP                  (gst_c2_comp_get_type ())
#define GST_C2_COMP(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_C2_COMP, GstC2Comp))
#define GST_IS_C2_COMP(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_C2_COMP))
#define GST_C2_COMP_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_C2_COMP, GstC2CompClass))
#define GST_IS_C2_COMP_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_C2_COMP))
#define GST_C2_COMP_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_C2_COMP, GstC2CompClass))
typedef struct _GstC2Comp GstC2Comp;
typedef struct _GstC2CompClass GstC2CompClass;

#define GST_BUFFER_POOL_OPTION_VIDEO_C2BUF_META "GstVideoC2BufMeta"

typedef enum
{
  DMABUF_MODE = 0,
  DMABUF_WRAP_MODE,
  FDBUF_MODE,
  FDBUF_WRAP_MODE
} PoolMode;

struct _GstBufferPoolInitParam
{
  GstVideoInfo info;
  GstC2Comp *gst_c2_comp;
  gboolean is_ubwc;
  PoolMode mode;
};

struct _GstQcodec2BufferPool
{
  GstBufferPool bufferpool;
  GstAllocator *allocator;
  gboolean add_c2bufmeta;
  GHashTable *buffer_table; /* only used in DMA/FD wrap mode */

  GstBufferPoolInitParam param;
};

struct _GstQcodec2BufferPoolClass
{
  GstBufferPoolClass parent_class;
};

typedef struct GstBufferPoolAcquireParamsExt
{
  GstBufferPoolAcquireParams params;
  gint32 fd;
  gint32 meta_fd;
  guint64 index;
  guint32 size;
  void *c2_buf;
} GstBufferPoolAcquireParamsExt;

GType gst_qcodec2_buffer_pool_get_type (void);
GstBufferPool *gst_qcodec2_buffer_pool_new (GstBufferPoolInitParam * param);

#define GST_VIDEO_C2BUF_META_API_TYPE  (gst_video_c2buf_meta_api_get_type())
#define GST_VIDEO_C2BUF_META_INFO  (gst_video_c2buf_meta_get_info())
typedef struct _GstVideoC2BufMeta GstVideoC2BufMeta;

/**
 * GstVideoC2BufMeta:
 * @meta: parent #GstMeta
 * @c2_buf: associated pointer of C2 Buffer
 *
 * Extra buffer metadata describing associated pointer of C2 Buffer.
 */
struct _GstVideoC2BufMeta
{
  GstMeta meta;

  void *c2_buf;
};

GType gst_video_c2buf_meta_api_get_type (void);
const GstMetaInfo *gst_video_c2buf_meta_get_info (void);

#define gst_buffer_get_video_c2buf_meta(b) ((GstVideoC2BufMeta*)gst_buffer_get_meta((b),GST_VIDEO_C2BUF_META_API_TYPE))
#define gst_buffer_add_video_c2buf_meta(b) ((GstVideoC2BufMeta*)gst_buffer_add_meta((b),GST_VIDEO_C2BUF_META_INFO, NULL))

struct _GstC2Comp
{
  GObject parent_instance;

  void *comp;
};

struct _GstC2CompClass
{
  GObjectClass parent_class;
};

GType gst_c2_comp_get_type (void);

GstC2Comp * gst_c2_comp_create (void * comp);

G_END_DECLS
#endif /* __GST_QCODEC2BUFFERPOOL_H__ */
