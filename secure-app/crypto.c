/*-------------------------------------------------------------------
Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------*/

/*
 * Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "crypto.h"
#include <dlfcn.h>
#include <stdio.h>

#define SymOEMCryptoLib "libcontentcopy.so"
#define SymOEMCryptoAppName "smpcpyap64"
#define SymOEMCryptoSetAppName "Content_Protection_Set_AppName"
#define SymOEMCryptoInit "Content_Protection_Copy_Init"
#define SymOEMCryptoTerminate "Content_Protection_Copy_Terminate"
#define SymOEMCryptoCopy "Content_Protection_Copy"

static char dlerr_cached_str[512] = {0};
char* dlerr_tip()
{
    //As dlerror() will clean that error string once it's called, need cache the string if need call dlerror() multiple times for one error
    char* new_err = dlerror();
    if (new_err) {
      snprintf(dlerr_cached_str, sizeof(dlerr_cached_str), "%s", new_err);
    }
    return dlerr_cached_str;
}


OMX_ERRORTYPE crypto_init(Crypto *crypto) {

    crypto->m_lib_handle = NULL;
    crypto->m_secure_handle = NULL;
    crypto->m_crypto_init = NULL;
    crypto->m_crypto_set_appname = NULL;
    crypto->m_crypto_deinit = NULL;
    crypto->m_crypto_copy = NULL;
    GST_DEBUG ("Crypto init");
    printf ("Crypto init\n");
    OMX_ERRORTYPE result = load_crypto_lib(crypto);
    if (result == OMX_ErrorNone) {
        if (crypto->m_crypto_init) {
            result = (OMX_ERRORTYPE)crypto->m_crypto_init(&crypto->m_secure_handle);
            if (crypto->m_crypto_set_appname) {
                result = (OMX_ERRORTYPE)crypto->m_crypto_set_appname(SymOEMCryptoAppName);
            } else {
                GST_ERROR("Invalid method handle to OEMCryptoSetAppName");
                printf("Invalid method handle to OEMCryptoSetAppName\n");
                result = OMX_ErrorBadParameter;
            }
        } else {
            GST_ERROR("Invalid method handle to OEMCryptoInit");
            printf("Invalid method handle to OEMCryptoInit\n");
            result = OMX_ErrorBadParameter;
        }
    }
    return result;
}

OMX_ERRORTYPE crypto_deinit(Crypto *crypto) {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    if (crypto->m_crypto_deinit) {
        result = (OMX_ERRORTYPE)crypto->m_crypto_deinit(&crypto->m_secure_handle);
    } else {
        GST_ERROR("Invalid method handle to OEMCryptoTerminate");
        printf("Invalid method handle to OEMCryptoTerminate\n");
        result = OMX_ErrorBadParameter;
    }
    unload_crypto_lib(crypto);
    return result;
}

OMX_ERRORTYPE crypto_copy(Crypto *crypto, SecureCopyDir eCopyDir,
        OMX_U8 *pBuffer, unsigned long nBufferFd, OMX_U32 *pBufferSize) {

    SecureCopyResult result = SECURE_COPY_SUCCESS;
    uint32 nBytesCopied = 0;
    uint32 nBufferSize = *pBufferSize;

    if (crypto->m_crypto_copy == NULL) {
        GST_ERROR("Invalid method handle to OEMCryptoCopy");
        printf("Invalid method handle to OEMCryptoCopy\n");
        return OMX_ErrorBadParameter;
    }

    GST_DEBUG ("CryptoCopy, fd: %u, buf: %p, size: %u, byte_ct: %u, copy_dir: %d",
        (unsigned int)nBufferFd, pBuffer, (unsigned int)nBufferSize, (unsigned int)nBytesCopied, eCopyDir);
    //printf ("CryptoCopy, fd: %u, buf: %p, size: %u, byte_ct: %u, copy_dir: %d\n",
    //    (unsigned int)nBufferFd, pBuffer, (unsigned int)nBufferSize, (unsigned int)nBytesCopied, eCopyDir);
    result = crypto->m_crypto_copy(crypto->m_secure_handle, pBuffer, nBufferSize,
            nBufferFd, 0, &nBytesCopied, eCopyDir);

    if (result != SECURE_COPY_SUCCESS) {
        GST_ERROR(
            "Error in CryptoCopy, fd: %u, buf: %p, size: %u, byte_ct: %u, copy_dir: %d result:%d",
            (unsigned int)nBufferFd, pBuffer, (unsigned int)nBufferSize, (unsigned int)nBytesCopied, eCopyDir, result);
        printf(
            "Error in CryptoCopy, fd: %u, buf: %p, size: %u, byte_ct: %u, copy_dir: %d result:%d\n",
            (unsigned int)nBufferFd, pBuffer, (unsigned int)nBufferSize, (unsigned int)nBytesCopied, eCopyDir, result);
        return OMX_ErrorBadParameter;
    }

    *pBufferSize = nBytesCopied;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE load_crypto_lib(Crypto *crypto) {

    OMX_ERRORTYPE result = OMX_ErrorNone;

    GST_DEBUG ("Loading crypto lib");
    printf ("Loading crypto lib\n");

    crypto->m_lib_handle = dlopen(SymOEMCryptoLib, RTLD_NOW);
    if (crypto->m_lib_handle == NULL) {
        GST_ERROR("Failed to open %s, error : %s", SymOEMCryptoLib, dlerr_tip());
        printf("Failed to open %s, error : %s\n", SymOEMCryptoLib, dlerr_tip());
        return OMX_ErrorUndefined;
    }

    crypto->m_crypto_set_appname = (Crypto_Set_AppName)dlsym(crypto->m_lib_handle, SymOEMCryptoSetAppName);
    if (crypto->m_crypto_set_appname == NULL) {
        GST_ERROR("Failed to find symbol for OEMCryptoInit: %s", dlerr_tip());
        printf("Failed to find symbol for OEMCryptoInit: %s\n", dlerr_tip());
        result = OMX_ErrorUndefined;
    }

    crypto->m_crypto_init = (Crypto_Init)dlsym(crypto->m_lib_handle, SymOEMCryptoInit);
    if (crypto->m_crypto_init == NULL) {
        GST_ERROR("Failed to find symbol for OEMCryptoInit: %s", dlerr_tip());
        printf("Failed to find symbol for OEMCryptoInit: %s\n", dlerr_tip());
        result = OMX_ErrorUndefined;
    }
    if (result == OMX_ErrorNone) {
        crypto->m_crypto_deinit = (Crypto_Deinit)dlsym(crypto->m_lib_handle, SymOEMCryptoTerminate);
        if (crypto->m_crypto_deinit == NULL) {
            GST_ERROR("Failed to find symbol for OEMCryptoTerminate: %s", dlerr_tip());
            printf("Failed to find symbol for OEMCryptoTerminate: %s\n", dlerr_tip());
            result = OMX_ErrorUndefined;
        }
    }
    if (result == OMX_ErrorNone) {
        crypto->m_crypto_copy = (Crypto_Copy)dlsym(crypto->m_lib_handle, SymOEMCryptoCopy);
        if (crypto->m_crypto_copy == NULL) {
            GST_ERROR("Failed to find symbol for OEMCryptoCopy: %s", dlerr_tip());
            printf("Failed to find symbol for OEMCryptoCopy: %s\n", dlerr_tip());
            result = OMX_ErrorUndefined;
        }
    }
    if (result != OMX_ErrorNone) {
        unload_crypto_lib(crypto);
    }
    return result;
}

void unload_crypto_lib(Crypto *crypto) {

    if (crypto->m_lib_handle) {
        dlclose(crypto->m_lib_handle);
        crypto->m_lib_handle = NULL;
        crypto->m_secure_handle = NULL;
    }
    crypto->m_crypto_init = NULL;
    crypto->m_crypto_set_appname = NULL;
    crypto->m_crypto_deinit = NULL;
    crypto->m_crypto_copy = NULL;
}



