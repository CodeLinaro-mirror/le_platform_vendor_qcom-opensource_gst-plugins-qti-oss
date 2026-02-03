/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _GST_QTISECUREFILESINK_H_
#define _GST_QTISECUREFILESINK_H_

#include <gst/base/gstbasesink.h>
#include <stdio.h>
#include <dlfcn.h>
#include <inttypes.h>
#include "cutils/native_handle.h"

G_BEGIN_DECLS

#define GST_TYPE_QTISECUREFILESINK   (gst_qtisecurefilesink_get_type())
#define GST_QTISECUREFILESINK(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QTISECUREFILESINK,GstQtisecurefilesink))
#define GST_QTISECUREFILESINK_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QTISECUREFILESINK,GstQtisecurefilesinkClass))
#define GST_IS_QTISECUREFILESINK(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QTISECUREFILESINK))
#define GST_IS_QTISECUREFILESINK_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QTISECUREFILESINK))

typedef struct _GstQtisecurefilesink GstQtisecurefilesink;
typedef struct _GstQtisecurefilesinkClass GstQtisecurefilesinkClass;
typedef struct _GstQtidummydrm GstQtidummydrm;
typedef struct _GstQtidummydrmClass GstQtidummydrmClass;

typedef enum SampleClientResult {
    SAMPLE_CLIENT_SUCCESS = 0,
    SAMPLE_CLIENT_ERROR_COPY_FAILED = 1,
    SAMPLE_CLIENT_ERROR_INIT_FAILED = 2,
    SAMPLE_CLIENT_ERROR_TERMINATE_FAILED = 3,
    SAMPLE_CLIENT_ERROR_ION_MALLOC_FAILED = 4,
    SAMPLE_CLIENT_ERROR_ION_FREE_FAILED = 5,
    SAMPLE_CLIENT_ERROR_NSS_COPY_FAILED = 6,
    SAMPLE_CLIENT_ERROR_SNS_COPY_FAILED = 7,
    SAMPLE_CLIENT_ERROR_MEM_SEG_COPY_FAILED = 8,
    SAMPLE_CLIENT_ERROR_INVALID_PARAMS = 9,
    SAMPLE_CLIENT_ERROR_FEATURE_NOT_SUPPORT = 10,
    SAMPLE_CLIENT_ERROR_BUFFER_TOO_SHORT = 11,
    SAMPLE_CLIENT_ERROR_SECURE_ION_MALLOC_FAILED = 12,
    SAMPLE_CLIENT_ERROR_FEATURE_NOT_SUPPORTED = 13,
    SAMPLE_CLIENT_FAILURE = 0x7FFFFFFF
} SampleClientResult;

typedef enum SampleClientCopyDir {
    SAMPLECLIENT_COPY_NONSECURE_TO_SECURE = 0,
    SAMPLECLIENT_COPY_SECURE_TO_NONSECURE,
    SAMPLECLIENT_COPY_INVALID_DIR
} SampleClientCopyDir;

typedef struct _QSEECom_handle QSEECom_handle;
struct _QSEECom_handle {
    unsigned char *ion_sbuffer;
};

typedef struct _GstDecryptorContextCPC GstDecryptorContextCPC;
struct _GstDecryptorContextCPC {
  SampleClientResult (*Content_Protection_Set_AppName)(const char*);
  SampleClientResult (*Content_Protection_Copy_Init)(QSEECom_handle**);
  SampleClientResult (*Content_Protection_Copy_Terminate)(QSEECom_handle**);
  SampleClientResult (*Content_Protection_Copy)(QSEECom_handle*, uint8_t*, const uint32_t,
                                              uint32_t, uint32_t, uint32_t*, SampleClientCopyDir);
};

struct _GstQtisecurefilesink
{
  GstBaseSink base_qtisecurefilesink;
  void *lib_handle;
  struct QSEECom_handle *l_QSEEComHandle;
  GstDecryptorContextCPC cpc;
  guint buf_size;
  guint8 * buf;
  FILE * fp;
};

struct _GstQtisecurefilesinkClass
{
  GstBaseSinkClass base_qtisecurefilesink_class;
};

GType gst_qtisecurefilesink_get_type (void);

G_END_DECLS

#endif
