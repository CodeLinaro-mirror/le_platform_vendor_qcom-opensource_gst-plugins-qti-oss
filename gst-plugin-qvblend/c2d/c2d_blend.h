/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef __c2d_blend_H__
#define __c2d_blend_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <c2d2.h>
#include <dlfcn.h>
#include "gbm.h"
#include "gbm_priv.h"
#include <linux/msm_kgsl.h>
#include <pthread.h>
#include <stdint.h>

#define ALIGN4K 4096
#define ALIGN2K 2048
#define ALIGN512 512
#define ALIGN256 256
#define ALIGN128 128
#define ALIGN64 64
#define ALIGN32 32
#define ALIGN16 16
#define ALIGN( num, to ) (((num) + (to-1)) & (~(to-1)))

enum ColorConvertFormat {
    RGB565 = 1,
    YCbCr420Tile,
    YCbCr420SP,
    YCbCr420P,
    YCrCb420P,
    RGBA8888,
    RGBA8888_UBWC,
    NV12_2K,
    NV12_128m,
    NV12_UBWC,
    TP10_UBWC,
    YCbCr420_VENUS_P010,
    P010,
    VENUS_P010,
    CbYCrY,
    BGR888,
    ARGB8888,
    RGBA8888_NO_PREMULTIPLIED,
    ARGB8888_NO_PREMULTIPLIED,
    NO_COLOR_FORMAT
};

typedef struct C2DBuffer{
    int fd;
    int handle;
    int size;
    int gbm_format;
    int width;
    int height;
    int pitch;
    int meta_fd;
    struct gbm_bo *gbm_bo;
    void *ptr;
}C2DBuffer;

typedef enum {
  C2D_INPUT = 0,
  C2D_OUTPUT,
} C2D_PORT;

class c2d_blend
{
public:
    c2d_blend();
    ~c2d_blend();
    bool init();
    void destroy();
    bool configure(unsigned int srcHeight,unsigned int srcWidth,
              unsigned int dstHeight, unsigned int dstWidth,
              ColorConvertFormat srcFormat, ColorConvertFormat dstFormat, unsigned int flag, unsigned int srcStride);
    bool convert(int srcFd, void *srcBase, void *srcData,
                 int dstFd, void *dstBase, void *dstData);
    int32_t dumpOutput(int fd);
    int blend(int x, int y,
               unsigned int srcWidth, unsigned int srcHeight,
               unsigned int dstWidth, unsigned int dstHeight,
               ColorConvertFormat srcFormat, ColorConvertFormat dstFormat);
    bool allocateBuffer(int port, struct C2DBuffer *buffer);
    bool freeBuffer(struct C2DBuffer *buffer);

private:
    uint32_t getC2DFormat(ColorConvertFormat format, bool isSource);
    size_t calcSize(ColorConvertFormat format, size_t width, size_t height);
    size_t calcYSize(ColorConvertFormat format, size_t width, size_t height);
    size_t calcStride(ColorConvertFormat format, size_t width);
    C2D_STATUS updateRGBSurface(void *gpuAddr, void *data, bool isSource);
    C2D_STATUS updateYUVSurface(void *gpuAddr, void *base, void *data, bool isSource);
    int32_t createSurface(ColorConvertFormat format, size_t width, size_t height, bool isSource);
    bool isYUVSurface(ColorConvertFormat format);
    void * mapGPUAddr(int bufFD, void *bufPtr, size_t bufLen);
    bool unmapGPUAddr(void *gAddr);
    void clearSurfaces();

private:
    pthread_mutex_t mLock;
    void *m_gbmhandle;
    int mGbmClientFd;
    int (*gbm_bo_get_fd)(struct gbm_bo *bo);
    int (*gbm_perform )(int operation,...);
    struct gbm_bo * (*gbm_bo_create)(struct gbm_device *gbm,uint32_t width, uint32_t height,uint32_t format, uint32_t flags);
    void (*gbm_bo_destroy)(struct gbm_bo *bo);
    uint32_t (*gbm_bo_get_width)(struct gbm_bo *bo);
    uint32_t (*gbm_bo_get_height)(struct gbm_bo *bo);
    uint32_t (*gbm_bo_get_stride)(struct gbm_bo *bo);
    void (*gbm_device_destroy)(struct gbm_device *gbm);
    struct gbm_device * (*gbm_create_device)(int fd);
    struct gbm_device *mGbmDev;
    bool mInit; // gbm init flag

    bool mConfigured; // C2D2 init flag
    C2D_OBJECT mBlit;
    uint32_t mSrcSurface;
    uint32_t mDstSurface;
    void *mSrcSurfaceDef;
    void *mDstSurfaceDef;

    ColorConvertFormat mSrcFormat;
    ColorConvertFormat mDstFormat;

    size_t mSrcWidth;
    size_t mSrcHeight;
    size_t mSrcStride;
    size_t mSrcSize;
    size_t mSrcYSize;
    size_t mDstWidth;
    size_t mDstHeight;
    size_t mDstSize;
    size_t mDstYSize;
};

typedef void (*compute_fmt_aligned_width_and_height_t)
  (int width, int height, int plane_id, int format, uint32_t num_samples,
   int tile_mode, int raster_mode, int padding_threshold,
   int *aligned_w, int *aligned_h);

class AdrenoLibLoader {
  public:
    static AdrenoLibLoader* instance(void) {
      static AdrenoLibLoader instance;
      return &instance;
    }

    /* Add this interface to reach goal of unit test branch coverage. */
    static void setAdrenoUtilsLibName(const char *name) {
      if (name)
        mAdrenoUtilsLibName = name;
    }

    compute_fmt_aligned_width_and_height_t mComputeAlignedWidthHeight;

  private:
    AdrenoLibLoader() {
      if (!loadLibrary())
        unloadLibrary();
    }
    AdrenoLibLoader(const AdrenoLibLoader&) = delete;
    AdrenoLibLoader& operator=(const AdrenoLibLoader&) = delete;
    ~AdrenoLibLoader() { unloadLibrary(); }

    bool loadLibrary(void) {
      const char *computeAlignedWidthHeight = "compute_fmt_aligned_width_and_height";

      mAdrenoUtilsHandle = dlopen(mAdrenoUtilsLibName, RTLD_NOW);
      if (nullptr == mAdrenoUtilsHandle) {
        GST_ERROR ("dlopen error %s: %s", mAdrenoUtilsLibName, dlerror());
        return false;
      }

      mComputeAlignedWidthHeight = (compute_fmt_aligned_width_and_height_t)
        dlsym(mAdrenoUtilsHandle, computeAlignedWidthHeight);
      if (nullptr == mComputeAlignedWidthHeight) {
        GST_ERROR ("dlsym error %s: %s", computeAlignedWidthHeight, dlerror());
        return false;
      }

      return true;
    }

    void unloadLibrary(void) {
      mComputeAlignedWidthHeight = nullptr;
      if (mAdrenoUtilsHandle) {
        dlclose(mAdrenoUtilsHandle);
        mAdrenoUtilsHandle = nullptr;
      }
    }

    void *mAdrenoUtilsHandle;
    static const char *mAdrenoUtilsLibName;
};

static inline void
    computeFormatAlignedWidthHeight(int width, int height,
    int format, int *aligned_w, int *aligned_h)
{
  AdrenoLibLoader *loader = AdrenoLibLoader::instance();

  if (nullptr != loader->mComputeAlignedWidthHeight) {
    int32_t tile_mode = 0;
    int32_t raster_mode = 0;
    int32_t padding_threshold = 512; /* hardcode for RGB formats */

    loader->mComputeAlignedWidthHeight(width, height, 0, format,
        1, tile_mode, raster_mode, padding_threshold,
        aligned_w, aligned_h);
  } else {
    *aligned_w = *aligned_h = 0;
  }
}

#endif
