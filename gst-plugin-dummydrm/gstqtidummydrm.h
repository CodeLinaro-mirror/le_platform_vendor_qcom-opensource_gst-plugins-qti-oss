/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _GST_QTIDUMMYDRM_H_
#define _GST_QTIDUMMYDRM_H_

#include <gst/base/gstbasetransform.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <gst/allocators/allocators.h>
#include <gst/memory/gstmempool.h>
#include "cutils/native_handle.h"
G_BEGIN_DECLS
#define GST_TYPE_QTIDUMMYDRM   (gst_qtidummydrm_get_type())
#define GST_QTIDUMMYDRM(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QTIDUMMYDRM,GstQtidummydrm))
#define GST_QTIDUMMYDRM_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QTIDUMMYDRM,GstQtidummydrmClass))
#define GST_IS_QTIDUMMYDRM(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QTIDUMMYDRM))
#define GST_IS_QTIDUMMYDRM_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QTIDUMMYDRM))
#define DEFAULT_BUFFER_SIZE       (1024*1024*2)
#define DEFAULT_MIN_BUFFERS       4
#define DEFAULT_MAX_BUFFERS       0
typedef struct _GstQtidummydrm GstQtidummydrm;
typedef struct _GstQtidummydrmClass GstQtidummydrmClass;

typedef enum SampleClientResult
{
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

typedef enum SampleClientCopyDir
{
  SAMPLECLIENT_COPY_NONSECURE_TO_SECURE = 0,
  SAMPLECLIENT_COPY_SECURE_TO_NONSECURE,
  SAMPLECLIENT_COPY_INVALID_DIR
} SampleClientCopyDir;

typedef struct _QSEECom_handle QSEECom_handle;
struct _QSEECom_handle
{
  unsigned char *ion_sbuffer;
};

typedef struct _GstDecryptorContextCPC GstDecryptorContextCPC;
struct _GstDecryptorContextCPC
{
  SampleClientResult (*Content_Protection_Set_AppName) (const char *);
  SampleClientResult (*Content_Protection_Copy_Init) (QSEECom_handle **);
  SampleClientResult (*Content_Protection_Copy_Terminate) (QSEECom_handle **);
  SampleClientResult (*Content_Protection_Copy) (QSEECom_handle *, uint8_t *,
      const uint32_t, uint32_t, uint32_t, uint32_t *, SampleClientCopyDir);
};

struct _GstQtidummydrm
{
  GstBaseTransform base_qtidummydrm;
  GstBufferPool *out_pool;
  void *lib_handle;
  struct QSEECom_handle *l_QSEEComHandle;
  GstDecryptorContextCPC cpc;
  guint pool_buf_size;
};

struct _GstQtidummydrmClass
{
  GstBaseTransformClass base_qtidummydrm_class;
};

GType gst_qtidummydrm_get_type (void);

G_END_DECLS
#endif
