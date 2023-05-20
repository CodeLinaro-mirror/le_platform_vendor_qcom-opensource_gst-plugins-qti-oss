/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <CryptoAPI.h>
#include <DrmAPI.h>
#include <dlfcn.h>
#include <gst/allocators/allocators.h>
#include "cutils/native_handle.h"
#include "decryptor-context.h"

#define GST_CAT_DEFAULT           gst_decryptor_context_debug_category ()
#define ALIGN(num, to)            (((num) + (to - 1)) & (~(to - 1)))
#define HEAP_ALIGN                4096
#define SECURE_DISPLAY_HEAP_ID    "system-secure"
#define SUBSAMPLE_INFO_LEN        6
#define CLEAR_BYTES_SIZE          2
#define ENCR_BYTES_SIZE           4
#define IV_SIZE                   16

static G_DEFINE_QUARK(QtiDecryptorQDataQuark, qti_decryptor_qdata);

using namespace android;

typedef CryptoFactory *(*CreateCryptoFactoryFunc)();

// playready uuid
static const guint8 g_pr_uuid[16] = {
  0x9A, 0x04, 0xF0, 0x79, 0x98, 0x40, 0x42, 0x86,
  0xAB, 0x92, 0xE6, 0x5B, 0xE0, 0x88, 0x5F, 0x95
};

struct _GstDecryptorContext {
  CryptoPlugin *pCrypto_plugin;
  CryptoFactory *pCrypto_factory;
  void *prcryptoLib_handle;
  native_handle_t *nh;
  BufferAllocator *buf_allocator;
};

static GstDebugCategory *
gst_decryptor_context_debug_category (void) {
  static gsize catonce = 0;

  if (g_once_init_enter (&catonce)) {
    gsize catdone = (gsize) _gst_debug_category_new ("qtidecryptor",
        0, "Decryptor Context");
    g_once_init_leave (&catonce, catdone);
  }
  return (GstDebugCategory *)catonce;
}

GstDecryptorContext *
gst_decryptor_context_new (const gchar *session_id)
{
  GstDecryptorContext *context = NULL;
  gchar *err = NULL;
  gulong prdrm_result = 0;
  gulong res = 0;
  guint idx = 0;
  gsize session_id_size = strlen(session_id);

  context = g_slice_new0 (GstDecryptorContext);
  g_return_val_if_fail (context != NULL, NULL);

  context->prcryptoLib_handle = dlopen ("/usr/lib/libprdrmengine.so", RTLD_NOW);

  CreateCryptoFactoryFunc createCryptoFactory =
      (CreateCryptoFactoryFunc)dlsym(context->prcryptoLib_handle,
                            "createCryptoFactory");

  if (createCryptoFactory == NULL)
  {
    if ((err = dlerror()) != NULL)
    {
      GST_ERROR_OBJECT (context, "Cannot find symbol, dlerror: %s", err);
      gst_decryptor_context_free (context);
      return NULL;
    }
  }

  context->pCrypto_factory = createCryptoFactory();

  prdrm_result = (context->pCrypto_factory)->createPlugin (g_pr_uuid,
      (void*)session_id, (size_t)session_id_size,
      &(context->pCrypto_plugin));

  if (prdrm_result != 0)
  {
    GST_ERROR_OBJECT (context, "DRM Create Crypto Plugin failed with error: %ld",
        prdrm_result);
    gst_decryptor_context_free (context);
    return NULL;
  }

  context->nh = native_handle_create(1 /*numFds*/, 0 /*numInts*/);
  if (context->nh == NULL)
  {
    GST_ERROR_OBJECT (context, "Invalid native handle");
    gst_decryptor_context_free (context);
    return NULL;
  }

  context->buf_allocator = CreateDmabufHeapBufferAllocator();
  if (context->buf_allocator == NULL)
  {
    GST_ERROR_OBJECT (context, "Failed to create Dmabuf allocator!");
    gst_decryptor_context_free (context);
    return NULL;
  }

  return context;
}

gboolean
gst_decryptor_context_free (GstDecryptorContext *context)
{
  if (context->pCrypto_plugin)
    delete context->pCrypto_plugin;

  if (context->pCrypto_factory)
    delete context->pCrypto_factory;

  if (context->prcryptoLib_handle)
    dlclose (context->prcryptoLib_handle);

  if (context->nh)
    native_handle_delete (context->nh);

  if (context->buf_allocator)
    FreeDmabufHeapBufferAllocator(context->buf_allocator);

  g_slice_free (GstDecryptorContext, context);
  return TRUE;
}

GstBuffer*
gst_decryptor_context_secure_buffer_allocate (BufferAllocator* buf_allocator, gsize size)
{
  GstBuffer *buffer = NULL;
  GstMemory *memory = NULL;
  GstAllocator *fd_allocator = NULL;
  const gchar* heap_id;
  gint fd = -1;
  gsize aligned_len;
  GstStructure *structure;

  heap_id = SECURE_DISPLAY_HEAP_ID;
  aligned_len = ALIGN (size, HEAP_ALIGN);

  fd = DmabufHeapAlloc(buf_allocator, heap_id, aligned_len, 0, 0);
  if (fd < 0)
  {
    GST_ERROR ("Failed to allocate secure buffer!");
    return NULL;
  }

  buffer = gst_buffer_new ();
  fd_allocator = gst_fd_allocator_new ();
  memory = gst_fd_allocator_alloc (fd_allocator, fd, aligned_len,
                                    GST_FD_MEMORY_FLAG_DONT_CLOSE);
  memory->size = size;
  gst_buffer_append_memory (buffer, memory);
  gst_object_unref (fd_allocator);

  structure = gst_structure_new_empty ("secure_gst_buffer");
  gst_structure_set (structure,
      "fd", G_TYPE_INT, fd,
      "size", G_TYPE_UINT, size,
      "aligned_len", G_TYPE_UINT, aligned_len,
      NULL);

  gst_mini_object_set_qdata (GST_MINI_OBJECT (buffer), qti_decryptor_qdata_quark (),
          structure, (GDestroyNotify) gst_decryptor_context_secure_buffer_release);

  return buffer;
}

void
gst_decryptor_context_secure_buffer_release (GstStructure * structure)
{
  guint size, aligned_len;
  gint fd;
  gint res = 0;

  gst_structure_get_int (structure, "fd", &fd);
  gst_structure_get_uint (structure, "size", &size);
  gst_structure_get_uint (structure, "aligned_len", &aligned_len);

  res = close (fd);
  if (res != 0)
  {
    GST_ERROR ("Failed to close fd=%d (size=%u, maxsize=%u) err=%d",
              fd, size, aligned_len, res);
  }

  gst_structure_free (structure);
}

gboolean
gst_decryptor_context_execute (GstDecryptorContext *context,
    GstBuffer *in_buffer, GstBuffer **out_buffer)
{
  GstProtectionMeta *encr_meta = gst_buffer_get_protection_meta (in_buffer);
  GstBuffer *key_id_buf, *iv_buf, *subsample_buf;
  GstMapInfo  inbuff_map_info, keyid_map_info, iv_map_info, subsample_map_info;
  CryptoPlugin::SubSample *subsample;
  CryptoPlugin::Pattern pattern;
  CryptoPlugin::Mode mode;
  AString *error_detail_msg;
  gsize inbuf_size, decrypt_size = 0;
  guint subsample_count, idx, total_bytes = 0;
  gboolean secure, result = true;
  guint8 iv_arr[IV_SIZE];
  inbuf_size = gst_buffer_get_size (in_buffer);

  if (!(*out_buffer = gst_decryptor_context_secure_buffer_allocate (
                                          context->buf_allocator, inbuf_size)))
  {
    GST_ERROR_OBJECT (context, "Failed to allocate secure buffer at output!");
    return false;
  }

  gst_structure_get_boolean (encr_meta->info, "encrypted", &secure);
  gst_structure_get_uint (encr_meta->info, "subsample_count", &subsample_count);

  subsample = g_new (CryptoPlugin::SubSample, subsample_count);

  key_id_buf = gst_value_get_buffer (
    gst_structure_get_value (encr_meta->info, "kid"));
  iv_buf = gst_value_get_buffer (
    gst_structure_get_value (encr_meta->info, "iv"));
  subsample_buf = gst_value_get_buffer (
    gst_structure_get_value (encr_meta->info, "subsamples"));

  gst_buffer_map (in_buffer, &inbuff_map_info, GST_MAP_READ);
  gst_buffer_map (subsample_buf, &subsample_map_info, GST_MAP_READ);
  gst_buffer_map (key_id_buf, &keyid_map_info, GST_MAP_READ);
  gst_buffer_map (iv_buf, &iv_map_info, GST_MAP_READ);

  /* Playready API expects IV of size 16 bytes. If the IV of input is of 8 bytes,
   * the remaining 8 bytes should be appended as 0.
   */
  memset (iv_arr, 0x00, IV_SIZE);
  for (idx = 0; idx < iv_map_info.size; idx++)
  {
    iv_arr[idx] = iv_map_info.data[idx];
  }

  /* Extract number of encrypted and clear bytes from subsample buffer of GstProtectionMeta
   * As per ISO/IEC CD 23001-7 spec, size of the subsample value should be 6 bytes
   * where MSB 2 bytes specify the number of clear bytes and the next 4 bytes specify
   * the number of encrypted bytes
   */
  for (idx = 0; idx < subsample_count; idx++)
  {
    guint pos = 0;
    subsample[idx].mNumBytesOfClearData = 0;
    subsample[idx].mNumBytesOfEncryptedData = 0;
    for (pos = 0; pos < SUBSAMPLE_INFO_LEN; pos++)
    {
      if (pos < CLEAR_BYTES_SIZE)
        subsample[idx].mNumBytesOfClearData =
            (subsample[idx].mNumBytesOfClearData << 8) |
                subsample_map_info.data[(SUBSAMPLE_INFO_LEN * idx) + pos];
      else
        subsample[idx].mNumBytesOfEncryptedData =
            (subsample[idx].mNumBytesOfEncryptedData << 8) |
                subsample_map_info.data[(SUBSAMPLE_INFO_LEN * idx) + pos];
    }

    GST_DEBUG_OBJECT (context, "Subsample(%d): Number of clear bytes=%u, encrypted bytes=%u",
                        idx, subsample[idx].mNumBytesOfClearData,
                        subsample[idx].mNumBytesOfEncryptedData);

    total_bytes += subsample[idx].mNumBytesOfClearData +
                          subsample[idx].mNumBytesOfEncryptedData;
  }

  /* Incase of byte-stream stream format in AVC, 6 bytes Access Unit Delimiter
   * is added at the start of each NAL unit. This offset needs to be accounted
   * in clear data bytes.
   */
  if (total_bytes < inbuf_size)
    subsample[0].mNumBytesOfClearData += inbuf_size - total_bytes;

  mode = CryptoPlugin::kMode_AES_CTR;
  context->nh->data[0] = gst_fd_memory_get_fd (
                            gst_buffer_get_memory (*out_buffer, 0));

  decrypt_size = context->pCrypto_plugin->decrypt (
    secure,
    keyid_map_info.data,
    iv_arr,
    mode,
    pattern,
    inbuff_map_info.data,
    subsample,
    subsample_count,
    (void *)(context->nh),
    error_detail_msg
  );

  if (decrypt_size != inbuf_size)
  {
    GST_ERROR_OBJECT (context, "Decrypted buffer size (%zu bytes) not equal to \
    input buffer size (%zu bytes)", decrypt_size, inbuf_size);
    result = false;
  }
  else
    GST_INFO_OBJECT (context, "Decrypted buffer size= %zu bytes \
     input size= %zu bytes", decrypt_size, inbuf_size);

  g_free (subsample);
  gst_buffer_unmap (in_buffer, &inbuff_map_info);
  gst_buffer_unmap (subsample_buf, &subsample_map_info);
  gst_buffer_unmap (key_id_buf, &keyid_map_info);
  gst_buffer_unmap (iv_buf, &iv_map_info);

  return result;
}
