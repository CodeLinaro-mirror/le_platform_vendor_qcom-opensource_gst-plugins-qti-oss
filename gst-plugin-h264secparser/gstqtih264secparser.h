/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _GST_QTIH264SECPARSER_H_
#define _GST_QTIH264SECPARSER_H_

#include <gst/base/gstbasetransform.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <gst/allocators/allocators.h>
#include <gst/memory/gstmempool.h>
#include "cutils/native_handle.h"
G_BEGIN_DECLS
#define GST_TYPE_QTIH264SECPARSER   (gst_qtih264secparser_get_type())
#define GST_QTIH264SECPARSER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_QTIH264SECPARSER,GstQtih264secparser))
#define GST_QTIH264SECPARSER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_QTIH264SECPARSER,GstQtih264secparserClass))
#define GST_IS_QTIH264SECPARSER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_QTIH264SECPARSER))
#define GST_IS_QTIH264SECPARSER_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_QTIH264SECPARSER))
#define TZ_OUT_BUF_POOL_SIZE_MAX 256
#define MAX_AU_UNITS TZ_OUT_BUF_POOL_SIZE_MAX

typedef struct _GstQtih264secparser GstQtih264secparser;
typedef struct _GstQtih264secparserClass GstQtih264secparserClass;

typedef struct _AUnitInfo
{
  guint offset;
  guint size;
} AUnitInfo;

typedef enum SecParserResult
{
  SEC_PARSER_SUCCESS                           = 0,
  SEC_PARSER_ERROR_PARSE_FAILED                = 1,
  SEC_PARSER_ERROR_INIT_FAILED                 = 2,
  SEC_PARSER_ERROR_TERMINATE_FAILED            = 3,
  SEC_PARSER_ERROR_ION_MALLOC_FAILED           = 4,
  SEC_PARSER_ERROR_ION_FREE_FAILED             = 5,
  SEC_PARSER_ERROR_NSS_COPY_FAILED             = 6,
  SEC_PARSER_ERROR_SNS_COPY_FAILED             = 7,
  SEC_PARSER_ERROR_MEM_SEC_COPY_FAILED         = 8,
  SEC_PARSER_ERROR_INVALID_PARAMS              = 9,
  SEC_PARSER_ERROR_FEATURE_NOT_SUPPORT         = 10,
  SEC_PARSER_ERROR_BUFFER_TOO_SHORT            = 11,
  SEC_PARSER_FAILURE = 0x7FFFFFFF
} SecParserResult;

typedef struct _QSEECom_handle QSEECom_handle;
struct _QSEECom_handle
{
  unsigned char *ion_sbuffer;
};

typedef struct _GstDecryptorContextHSP GstDecryptorContextHSP;
struct _GstDecryptorContextHSP
{
  SecParserResult (*H264_Secure_Parser_Set_AppName) (const char *);
  SecParserResult (*H264_Secure_Parser_Init) (QSEECom_handle **);
  SecParserResult (*H264_Secure_Parser_Terminate) (QSEECom_handle **);
  SecParserResult (*H264_Secure_Parser) (QSEECom_handle *, const unint32_t,
    const uint32_t, const uint32_t, uint32_t *, uint32_t **, uint32_t **);
  SecParserResult (*Secure_Mem_Copy) (QSEECom_handle *, const uint32_t,
    const uint32_t, const uint32_t, const uint32_t, const uint32_t, const uint32_t);
};

struct _GstQtih264secparser
{
  GstBaseTransform base_qtih264secparser;
  void *lib_handle;
  struct QSEECom_handle *l_QSEEComHandle;
  GstDecryptorContextHSP hsp;
  GstBufferPool *out_pool;
  guint pool_buf_size;
  guint min_out_buffers;

  /* Buffer splitting fields */
  GstBuffer *input_buffer;
  guint current_au_index;
  guint au_count;
  AUnitInfo au_units[MAX_AU_UNITS];

  /* Properties */
  gboolean passthrough;
};

struct _GstQtih264secparserClass
{
  GstBaseTransformClass base_qtih264secparser_class;
};

GType gst_qtih264secparser_get_type (void);

G_END_DECLS
#endif
