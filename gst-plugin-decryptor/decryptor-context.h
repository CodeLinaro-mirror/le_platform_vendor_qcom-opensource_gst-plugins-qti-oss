/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __GST_DECRYPTOR_CONTEXT_H__
#define __GST_DECRYPTOR_CONTEXT_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <BufferAllocator/BufferAllocatorWrapper.h>

G_BEGIN_DECLS

#define GST_DECRYPTOR_CONTEXT(obj)    ((GstDecryptorContext*)(obj))

typedef struct _GstDecryptorContext GstDecryptorContext;

GST_API GstDecryptorContext *
gst_decryptor_context_new (const gchar *session_id);

GST_API gboolean
gst_decryptor_context_free (GstDecryptorContext *context);

GST_API gboolean
gst_decryptor_context_execute (GstDecryptorContext *context,
    GstBuffer *in_buffer, GstBuffer **out_buffer);

GST_API GstBuffer *
gst_decryptor_context_secure_buffer_allocate (BufferAllocator* buf_allocator,
    gsize size);

GST_API void
gst_decryptor_context_secure_buffer_release (GstStructure * structure);

G_END_DECLS

#endif