/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __GST_DECRYPTOR_H__
#define __GST_DECRYPTOR_H__

#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#include <stdio.h>
#include "decryptor-context.h"

G_BEGIN_DECLS

#define GST_TYPE_DECRYPTOR \
  (gst_decryptor_get_type())
#define GST_DECRYPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_DECRYPTOR, GstDecryptor))
#define GST_DECRYPTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DECRYPTOR, GstDecryptorClass))
#define GST_IS_DECRYPTOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_DECRYPTOR))
#define GST_IS_DECRYPTOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DECRYPTOR))
#define GST_DECRYPTOR_CAST(obj)         ((GstDecryptor *)(obj))

typedef struct _GstDecryptor GstDecryptor;
typedef struct _GstDecryptorClass GstDecryptorClass;

struct _GstDecryptor {
  GstElement              parent;
  GstPad                  *srcpad,
                          *sinkpad;
  gchar                   *session_id;
  GstDecryptorContext     *context;
};

struct _GstDecryptorClass {
  GstElementClass   parent;
};

G_GNUC_INTERNAL GType gst_decryptor_get_type (void);

G_END_DECLS

#endif