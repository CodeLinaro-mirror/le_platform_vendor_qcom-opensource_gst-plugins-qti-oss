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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <gst/gstinfo.h>
#include <gralloc_priv.h>
#include <vidc/media/msm_media_info.h>
#include "c2d_blend.h"

/* For ADRENO_PIXELFORMAT_XXX definitions
 * used by computeFormatAlignedWidthHeight(). */
#include <msmgbm_adreno_utils.h>

GST_DEBUG_CATEGORY_EXTERN (gst_videoblend_debug);
#define GST_CAT_DEFAULT gst_videoblend_debug

c2d_blend::c2d_blend():
    mGbmClientFd(-1), mGbmDev(NULL),
    mInit(false), mConfigured(false),
    mSrcSurface(0), mDstSurface(0),
    mSrcSurfaceDef(nullptr), mDstSurfaceDef(nullptr),
    mSrcFormat(RGBA8888), mDstFormat(NO_COLOR_FORMAT),
    mSrcWidth(0), mSrcHeight(0), mSrcStride(0), mSrcSize(0), mSrcYSize(0),
    mDstWidth(0), mDstHeight(0), mDstSize(0), mDstYSize(0)
{
    pthread_mutex_init(&mLock, NULL);
    memset(&mBlit,0,sizeof(C2D_OBJECT));
}

c2d_blend::~c2d_blend()
{
    if (mGbmDev && gbm_device_destroy)
        gbm_device_destroy (mGbmDev);
    mGbmDev = NULL;
    if (mGbmClientFd != -1)
        close(mGbmClientFd);
    mGbmClientFd = -1;
    clearSurfaces();
    pthread_mutex_destroy(&mLock);
}

bool c2d_blend::init()
{
    bool bStatus = true;

    if (mInit)
        bStatus = false;

    if (bStatus)
    {
        m_gbmhandle = dlopen("libgbm.so", RTLD_NOW);
        if (m_gbmhandle)
        {
            gbm_create_device = (struct gbm_device * (*)(int)) dlsym(m_gbmhandle,"gbm_create_device");
            gbm_device_destroy = (void (*)(struct gbm_device *)) dlsym(m_gbmhandle,"gbm_device_destroy");
            gbm_bo_get_height = (uint32_t (*)(struct gbm_bo *)) dlsym(m_gbmhandle,"gbm_bo_get_height");
            gbm_bo_get_stride = (uint32_t (*)(struct gbm_bo *)) dlsym(m_gbmhandle,"gbm_bo_get_stride");
            gbm_bo_create = (struct gbm_bo * (*)(struct gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t)) dlsym(m_gbmhandle,"gbm_bo_create");
            gbm_bo_destroy = (void (*)(struct gbm_bo *)) dlsym(m_gbmhandle,"gbm_bo_destroy");
            gbm_bo_get_fd = (int (*)(struct gbm_bo *)) dlsym(m_gbmhandle,"gbm_bo_get_fd");
            gbm_perform = (int (* )(int, ...)) dlsym(m_gbmhandle,"gbm_perform");
            GST_DEBUG("gbm %p %p %p %p %p %p %p %p",
                gbm_create_device,
                gbm_device_destroy,
                gbm_bo_get_height,
                gbm_bo_get_stride,
                gbm_bo_create,
                gbm_bo_destroy,
                gbm_bo_get_fd,
                gbm_perform);
            if (!gbm_create_device || !gbm_device_destroy
                || !gbm_bo_get_height || !gbm_bo_get_stride
                || !gbm_bo_create || !gbm_bo_destroy
                || !gbm_bo_get_fd || !gbm_perform)
                bStatus = false;
        }
        else
        {
            bStatus = false;
        }
    }

    if (bStatus)
    {
        mGbmClientFd = ::open ("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
        if (mGbmClientFd < 0)
        {
            GST_ERROR("failed to open gbm device");
            bStatus = false;
        }

        mGbmDev = gbm_create_device (mGbmClientFd);
        if (NULL == mGbmDev)
        {
            GST_ERROR ("failed to create gbm_device");
            bStatus = false;
        }
    }

    if (!bStatus)
    {
        if (mGbmDev && gbm_device_destroy)
            gbm_device_destroy (mGbmDev);
        mGbmDev = NULL;
        if (mGbmClientFd != -1) close (mGbmClientFd);
        mGbmClientFd = -1;
    }
    if(bStatus) mInit = true;
    return bStatus;
}

bool c2d_blend::convert(int srcFd, void *srcBase, void *srcData,
                        int dstFd, void *dstBase, void *dstData)
{
    C2D_STATUS ret;
    void *srcMappedGpuAddr = nullptr;
    void *dstMappedGpuAddr = nullptr;
    bool status = false;

    if (srcFd < 0 || dstFd < 0
        || srcData == NULL || dstData == NULL
        || srcBase == NULL || dstBase == NULL)
    {
        GST_ERROR("Color conversion failed. Incorrect input parameters FD (%d:%d) Data (%p:%p) Base (%p:%p)",
            srcFd, dstFd, srcData, dstData, srcBase, dstBase);
        return false;
    }
    pthread_mutex_lock(&mLock);
    if (!mConfigured)
    {
        GST_ERROR("Converter not enabled yet");
        goto out;
    }

    srcMappedGpuAddr = mapGPUAddr(srcFd, srcData, mSrcSize);
    if (!srcMappedGpuAddr)
    {
        goto out;
    }

    if (isYUVSurface(mSrcFormat))
    {
        ret = updateYUVSurface(srcMappedGpuAddr, srcBase, srcData, true);
    }
    else
    {
        ret = updateRGBSurface(srcMappedGpuAddr, srcData, true);
    }

    if (ret == C2D_STATUS_OK)
    {
        dstMappedGpuAddr = mapGPUAddr(dstFd, dstData, mDstSize);
        if (!dstMappedGpuAddr)
        {
            goto unmap_src;
        }

        if (isYUVSurface(mDstFormat))
        {
            ret = updateYUVSurface(dstMappedGpuAddr, dstBase, dstData, false);
        }
        else
        {
            ret = updateRGBSurface(dstMappedGpuAddr, dstData, false);
        }

        if (ret == C2D_STATUS_OK)
        {
            mBlit.surface_id = mSrcSurface;
            ret = c2dDraw(mDstSurface, C2D_TARGET_ROTATE_0, 0, 0, 0, &mBlit, 1);
            c2dFinish(mDstSurface);

            if (ret == C2D_STATUS_OK)
            {
                status = true;
            }
            else
            {
                GST_ERROR("C2D Draw failed (%d)", ret);
                goto unmap_all;
            }
        }
        else
        {
            GST_ERROR("Update dst surface def failed (%d)", ret);
            goto unmap_all;
        }
    }
    else
    {
        GST_ERROR("Update src surface def failed (%d)", ret);
        goto unmap_src;
    }
unmap_all:
    if (!unmapGPUAddr(dstMappedGpuAddr))
    {
        GST_ERROR("unmapping dst GPU address failed");
        status = false;
  }
unmap_src:
    if (!unmapGPUAddr(srcMappedGpuAddr))
    {
        GST_ERROR("unmapping src GPU address failed");
        status = false;
    }
out:
    pthread_mutex_unlock(&mLock);
    return status;
}

bool c2d_blend::configure(unsigned int srcHeight,unsigned int srcWidth,
                     unsigned int dstHeight, unsigned int dstWidth,
                     ColorConvertFormat srcFormat, ColorConvertFormat dstFormat, unsigned int flag, unsigned int srcStride)
{
    int32_t retval = -1;
    pthread_mutex_lock(&mLock);
    if (mConfigured)
    {
        GST_ERROR("Already enabled");
        goto out;
    }

    clearSurfaces();

    mSrcWidth = srcWidth;
    mSrcHeight = srcHeight;
    mSrcStride = srcStride;
    mDstWidth = dstWidth;
    mDstHeight = dstHeight;
    mSrcFormat = srcFormat;
    mDstFormat = dstFormat;

    mSrcSize = calcSize(srcFormat, mSrcWidth, mSrcHeight);
    mDstSize = calcSize(dstFormat, mDstWidth, mDstHeight);
    mSrcYSize = calcYSize(srcFormat, mSrcWidth, mSrcHeight);
    mDstYSize = calcYSize(dstFormat, mDstWidth, mDstHeight);

    retval = createSurface(srcFormat, mSrcWidth, mSrcHeight, true);
    retval |= createSurface(dstFormat, mDstWidth, mDstHeight, false);

    if (retval != 0)
    {
        GST_ERROR("Create surfaces failed");
        goto out;
    }
    memset((void*)&mBlit,0,sizeof(C2D_OBJECT));
    mBlit.source_rect.x = 0 << 16;
    mBlit.source_rect.y = 0 << 16;
    mBlit.source_rect.width = srcWidth << 16;
    mBlit.source_rect.height = srcHeight << 16;
    mBlit.target_rect.x = 0 << 16;
    mBlit.target_rect.y = 0 << 16;
    mBlit.target_rect.width = dstWidth << 16;
    mBlit.target_rect.height = dstHeight << 16;
    mBlit.config_mask = C2D_ALPHA_BLEND_NONE    |
        C2D_NO_BILINEAR_BIT     |
        C2D_NO_ANTIALIASING_BIT |
        C2D_TARGET_RECT_BIT;
    mBlit.surface_id = mSrcSurface;
    mConfigured = true;

out:
    pthread_mutex_unlock(&mLock);
    return retval == 0 ? true : false;
}

void c2d_blend::destroy()
{
    pthread_mutex_lock(&mLock);
    GST_INFO ("configured=%d, srcFormat=%d, dstFormat=%d, srcWidth=%lu, srcHeight=%lu, dstWidth=%lu, dstHeight=%lu",
               mConfigured, mSrcFormat, mDstFormat, mSrcWidth, mSrcHeight, mDstWidth, mDstHeight);
    if (mConfigured)
    {
        clearSurfaces();
        mConfigured = false;
    }
    pthread_mutex_unlock(&mLock);
}

bool c2d_blend::allocateBuffer(int port,struct C2DBuffer *buffer)
{
    if ((port != C2D_INPUT) && (port != C2D_OUTPUT))
    {
        GST_ERROR("bad port param, port = %d",port);
        return false;
    }

    if (!buffer)
    {
        GST_ERROR("Buffer param pointer is NULL");
        return false;
    }

    if (!buffer->width || !buffer->height)
    {
        GST_ERROR("Buffer param width/height is NULL");
        return false;
    }
    if (!buffer->gbm_format) {
        GST_ERROR("Buffer format is NULL");
        return false;
    }

    buffer->gbm_bo = gbm_bo_create (mGbmDev, buffer->width, buffer->height, buffer->gbm_format, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING | GBM_BO_USE_WRITE);
    if (NULL == buffer->gbm_bo) {
        GST_ERROR ("failed to create a bo");
        return false;
    }

    buffer->fd = -1;
    buffer->meta_fd = -1;
    buffer->fd = gbm_bo_get_fd(buffer->gbm_bo);
    gbm_perform (GBM_PERFORM_GET_METADATA_ION_FD, buffer->gbm_bo, &buffer->meta_fd);
    if (buffer->fd <= 0 || buffer->meta_fd <= 0)
    {
        GST_ERROR ("the fds(bo_fd:%d, meta_fd:%d) are invalid",
                   buffer->fd, buffer->meta_fd);
        if (buffer->fd >= 0) {
            close (buffer->fd);
        }
        gbm_bo_destroy (buffer->gbm_bo);
        buffer->gbm_bo = NULL;
        buffer->fd = -1;
        buffer->meta_fd = -1;
        return false;
    }

    buffer->pitch = gbm_bo_get_stride (buffer->gbm_bo);
    int result;

    result = gbm_perform(GBM_PERFORM_GET_BO_SIZE, buffer->gbm_bo, &buffer->size);
    if (GBM_ERROR_NONE != result)
    {
        GST_ERROR ("ERROR: get length error");
        if (buffer->fd >= 0) {
            close (buffer->fd);
        }
        gbm_bo_destroy (buffer->gbm_bo);
        buffer->gbm_bo = NULL;
        buffer->fd = -1;
        buffer->meta_fd = -1;
        return false;
    }

    void *va = mmap(NULL, buffer->size, PROT_READ|PROT_WRITE, MAP_SHARED, buffer->fd, 0);
    if (MAP_FAILED == va)
    {
        GST_ERROR("failed to map buffer of size = %u, fd = 0x%x",
                  buffer->size, buffer->fd);
        if (buffer->fd >= 0) {
            close (buffer->fd);
        }
        gbm_bo_destroy (buffer->gbm_bo);
        buffer->gbm_bo = NULL;
        buffer->fd = -1;
        buffer->meta_fd = -1;
        return false;
    }

    buffer->ptr = va;
    return true;
}

bool c2d_blend::freeBuffer(struct C2DBuffer *buffer)
{
    if (!buffer)
    {
        GST_ERROR("Buffer param pointer is NULL");
        return false;
    }

    if (buffer->ptr)
    {
        munmap (buffer->ptr, buffer->size);
        buffer->ptr = NULL;
    }

    if (buffer->gbm_bo)
    {
        if (buffer->fd >= 0)
        {
            close (buffer->fd);
        }
        gbm_bo_destroy (buffer->gbm_bo);
    }
    buffer->gbm_bo = NULL;
    buffer->fd = -1;
    buffer->meta_fd = -1;

    return true;
}

int32_t c2d_blend::dumpOutput(int fd)
{
    size_t stride, sliceHeight;
    if (fd < 0)
    {
        GST_ERROR("fd invalid");
        return -1;
    }

    int ret = 0;
    if (isYUVSurface(mDstFormat))
    {
        C2D_YUV_SURFACE_DEF * dstSurfaceDef = (C2D_YUV_SURFACE_DEF *)mDstSurfaceDef;
        uint8_t * base = (uint8_t *)dstSurfaceDef->plane0;
        stride = dstSurfaceDef->stride0;
        sliceHeight = dstSurfaceDef->height;
        /* dump luma */
        for (size_t i = 0; i < sliceHeight; i++)
        {
            ret = write(fd, base, mDstWidth); //will work only for the 420 ones
            if (ret < 0)
                goto cleanup;
            base += stride;
        }

        base = (uint8_t *)dstSurfaceDef->plane1;
        stride = dstSurfaceDef->stride1;
        for (size_t i = 0; i < (sliceHeight+1) / 2; i++)
        {
            ret = write(fd, base, mDstWidth);
            if (ret < 0)
                goto cleanup;
            base += stride;
        }
    }
    else
    {
        C2D_RGB_SURFACE_DEF * dstSurfaceDef = (C2D_RGB_SURFACE_DEF *)mDstSurfaceDef;
        uint8_t * base = (uint8_t *)dstSurfaceDef->buffer;
        stride = dstSurfaceDef->stride;
        sliceHeight = dstSurfaceDef->height;

        GST_INFO("rgb surface base is %p", base);
        GST_INFO("rgb surface dumpsslice height is %lu", (unsigned long)sliceHeight);
        GST_INFO("rgb surface dump stride is %lu", (unsigned long)stride);

        int bpp = 1; //bytes per pixel
        if (mDstFormat == ARGB8888 || mDstFormat == RGBA8888
                || mDstFormat == ARGB8888_NO_PREMULTIPLIED
                || mDstFormat == RGBA8888_NO_PREMULTIPLIED)
        {
            bpp = 4;
        }

        int count = 0;
        for (size_t i = 0; i < sliceHeight; i++)
        {
            ret = write(fd, base, mDstWidth*bpp);
            if (ret < 0)
            {
                GST_ERROR("Write failed, count = %d", count);
                goto cleanup;
            }
            base += stride;
            count += stride;
        }
    }
cleanup:
    if (ret < 0)
    {
        GST_ERROR("File write failed w/ errno %s", strerror(errno));
    }
    close(fd);
    return ret < 0 ? ret : 0;
}

int c2d_blend::blend(int x, int y, unsigned int srcWidth, unsigned int srcHeight, unsigned int dstWidth, unsigned int dstHeight, ColorConvertFormat srcFormat, ColorConvertFormat dstFormat)
{
    int32_t retval = -1;
    pthread_mutex_lock(&mLock);
    if (!mConfigured)
    {
        GST_ERROR("Converter not enabled yet");
        goto out;
    }

    clearSurfaces();

    mSrcWidth = srcWidth;
    mSrcHeight = srcHeight;
    mSrcStride = 0;
    mDstWidth = dstWidth;
    mDstHeight = dstHeight;
    mSrcFormat = srcFormat;
    mDstFormat = dstFormat;
    mSrcSize = calcSize(srcFormat, srcWidth, srcHeight);
    mDstSize = calcSize(dstFormat, dstWidth, dstHeight);
    mSrcYSize = calcYSize(srcFormat, srcWidth, srcHeight);
    mDstYSize = calcYSize(dstFormat, dstWidth, dstHeight);

    retval = createSurface(srcFormat, srcWidth, srcHeight, true);
    retval |= createSurface(dstFormat, dstWidth, dstHeight, false);

    if (retval != 0)
    {
        GST_ERROR("Update surfaces failed");
        goto out;
    }
    mBlit.source_rect.x = 0 << 16;
    mBlit.source_rect.y = 0 << 16;
    mBlit.source_rect.width = srcWidth << 16;
    mBlit.source_rect.height = srcHeight << 16;
    mBlit.target_rect.x = x << 16;
    mBlit.target_rect.y = y << 16;
    mBlit.target_rect.width = mSrcWidth << 16;
    mBlit.target_rect.height = mSrcHeight << 16;
    mBlit.config_mask = C2D_TARGET_RECT_BIT | C2D_ALPHA_BLEND_SRC_OVER;
    mBlit.surface_id = mSrcSurface;
    GST_INFO("C2D library: target rect x = %d, y = %d, width = %zu, height = %zu, retval = %d", x, y, mSrcWidth, mSrcHeight, retval);

out:
    pthread_mutex_unlock(&mLock);
    return retval;
}

uint32_t c2d_blend::getC2DFormat(ColorConvertFormat format, bool isSource)
{
    uint32_t C2DFormat;
    switch (format)
    {
        case ARGB8888:
            C2DFormat = C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_SWAP_ENDIANNESS;
            if (isSource)
                C2DFormat |= C2D_FORMAT_PREMULTIPLIED;
            return C2DFormat;
        case ARGB8888_NO_PREMULTIPLIED:
            C2DFormat = C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_SWAP_ENDIANNESS;
            return C2DFormat;
        case RGBA8888:
            C2DFormat = C2D_COLOR_FORMAT_8888_RGBA | C2D_FORMAT_SWAP_ENDIANNESS;
            if (isSource)
                C2DFormat |= C2D_FORMAT_PREMULTIPLIED;
            return C2DFormat;
        case RGBA8888_NO_PREMULTIPLIED:
            C2DFormat = C2D_COLOR_FORMAT_8888_RGBA | C2D_FORMAT_SWAP_ENDIANNESS;
            return C2DFormat;
        case NV12_128m:
            return C2D_COLOR_FORMAT_420_NV12;
        default:
            GST_WARNING("Format not supported , %d", format);
            return -1;
    }
}

size_t c2d_blend::calcSize(ColorConvertFormat format, size_t width, size_t height)
{
    int32_t alignedw = 0;
    int32_t alignedh = 0;
    int32_t size = 0;
    int32_t bpp = 0;

    switch (format)
    {
        case ARGB8888:
        case ARGB8888_NO_PREMULTIPLIED:
        case RGBA8888:
        case RGBA8888_NO_PREMULTIPLIED:
            bpp = 4;
            computeFormatAlignedWidthHeight(width, height,
                                            msm_gbm::ADRENO_PIXELFORMAT_R8G8B8A8,
                                            &alignedw, &alignedh);
            GST_DEBUG("Format: %d, alignedw %d alignedh %d", format, alignedw, alignedh);
            if (mSrcStride)
                size = mSrcStride *  alignedh * bpp;
            else
                size = alignedw * alignedh * bpp;
            size = ALIGN(size, ALIGN4K);
            break;
        case NV12_128m:
            alignedw = VENUS_Y_STRIDE(COLOR_FMT_NV12, width);
            alignedh = VENUS_Y_SCANLINES(COLOR_FMT_NV12, height);
            size = ALIGN(alignedw * alignedh + (alignedw * ALIGN((height+1)/2, VENUS_Y_SCANLINES(COLOR_FMT_NV12, 1)/2)), ALIGN4K);
            break;
        default:
            GST_WARNING("Format not supported , %d", format);
            break;
    }
    return size;
}

size_t c2d_blend::calcYSize(ColorConvertFormat format, size_t width, size_t height)
{
    switch (format)
    {
        case NV12_128m: {
            int32_t stride_alignment = VENUS_Y_STRIDE(COLOR_FMT_NV12, 1);
            int32_t scanline_alignment = VENUS_Y_SCANLINES(COLOR_FMT_NV12, 1);
            return ALIGN(width, stride_alignment) * ALIGN(height, scanline_alignment);
        }
        default:
            GST_DEBUG("Format %d is not needed to handle", format);
            return 0;
    }
}

size_t c2d_blend::calcStride(ColorConvertFormat format, size_t width)
{
    switch (format)
    {
        case ARGB8888:
        case ARGB8888_NO_PREMULTIPLIED:
        case RGBA8888:
        case RGBA8888_NO_PREMULTIPLIED:
            if (mSrcStride)
                return mSrcStride * 4;
            else
                return ALIGN(width, ALIGN32) * 4;
        case NV12_128m: {
            int32_t stride_alignment = VENUS_Y_STRIDE(COLOR_FMT_NV12, 1);
            return ALIGN(width, stride_alignment);
        }
        default:
            GST_WARNING("Format not supported , %d", format);
            return 0;
    }
}

C2D_STATUS c2d_blend::updateRGBSurface(void *gpuAddr, void * data, bool isSource)
{
    if (isSource)
    {
        C2D_RGB_SURFACE_DEF * srcSurfaceDef = (C2D_RGB_SURFACE_DEF *)mSrcSurfaceDef;
        srcSurfaceDef->buffer = data;
        srcSurfaceDef->phys = gpuAddr;
        return  c2dUpdateSurface(mSrcSurface, C2D_SOURCE,
                        (C2D_SURFACE_TYPE)(C2D_SURFACE_RGB_HOST | C2D_SURFACE_WITH_PHYS),
                        &(*srcSurfaceDef));
    }
    else
    {
        C2D_RGB_SURFACE_DEF * dstSurfaceDef = (C2D_RGB_SURFACE_DEF *)mDstSurfaceDef;
        dstSurfaceDef->buffer = data;
        GST_DEBUG("dstSurfaceDef->buffer = %p", data);
        dstSurfaceDef->phys = gpuAddr;
        return c2dUpdateSurface(mDstSurface, C2D_TARGET,
                        (C2D_SURFACE_TYPE)(C2D_SURFACE_RGB_HOST | C2D_SURFACE_WITH_PHYS),
                        &(*dstSurfaceDef));
    }
}

C2D_STATUS c2d_blend::updateYUVSurface(void *gpuAddr, void *base, void *data, bool isSource)
{
    if (isSource)
    {
        C2D_YUV_SURFACE_DEF * srcSurfaceDef = (C2D_YUV_SURFACE_DEF *)mSrcSurfaceDef;
        srcSurfaceDef->plane0 = data;
        srcSurfaceDef->phys0  = (uint8_t *)gpuAddr + ((uint8_t *)data - (uint8_t *)base);
        srcSurfaceDef->plane1 = (uint8_t *)data + mSrcYSize;
        srcSurfaceDef->phys1  = (uint8_t *)srcSurfaceDef->phys0 + mSrcYSize;
        return c2dUpdateSurface(mSrcSurface, C2D_SOURCE,
                        (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS),
                        &(*srcSurfaceDef));
    }
    else
    {
        C2D_YUV_SURFACE_DEF * dstSurfaceDef = (C2D_YUV_SURFACE_DEF *)mDstSurfaceDef;
        dstSurfaceDef->plane0 = data;
        dstSurfaceDef->phys0  = (uint8_t *)gpuAddr + ((uint8_t *)data - (uint8_t *)base);
        dstSurfaceDef->plane1 = (uint8_t *)data + mDstYSize;
        dstSurfaceDef->phys1  = (uint8_t *)dstSurfaceDef->phys0 + mDstYSize;
        return c2dUpdateSurface(mDstSurface, C2D_TARGET,
                        (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS),
                        &(*dstSurfaceDef));
    }
}

int32_t c2d_blend::createSurface(ColorConvertFormat format,
                                            size_t width, size_t height,
                                            bool isSource)
{
    void *surfaceDef = NULL;
    C2D_SURFACE_TYPE hostSurfaceType;
    C2D_STATUS ret;

    if (isYUVSurface(format))
    {
        C2D_YUV_SURFACE_DEF **surfaceYUVDef = (C2D_YUV_SURFACE_DEF **)
                                  (isSource ? &mSrcSurfaceDef : &mDstSurfaceDef);
        if (*surfaceYUVDef == NULL)
        {
            *surfaceYUVDef = (C2D_YUV_SURFACE_DEF *)
                                  calloc(1, sizeof(C2D_YUV_SURFACE_DEF));
            if (*surfaceYUVDef == NULL)
            {
                GST_ERROR("surfaceYUVDef allocation failed");
                return -1;
            }
        }
        else
        {
            memset(*surfaceYUVDef, 0, sizeof(C2D_YUV_SURFACE_DEF));
        }
        (*surfaceYUVDef)->format = getC2DFormat(format, isSource);
        (*surfaceYUVDef)->width = width;
        (*surfaceYUVDef)->height = height;
        (*surfaceYUVDef)->plane0 = (void *)0xaaaaaaaa;
        (*surfaceYUVDef)->phys0 = (void *)0xaaaaaaaa;
        (*surfaceYUVDef)->stride0 = calcStride(format, width);
        (*surfaceYUVDef)->plane1 = (void *)0xaaaaaaaa;
        (*surfaceYUVDef)->phys1 = (void *)0xaaaaaaaa;
        (*surfaceYUVDef)->stride1 = calcStride(format, width);
        (*surfaceYUVDef)->stride2 = calcStride(format, width);
        (*surfaceYUVDef)->phys2 = NULL;
        (*surfaceYUVDef)->plane2 = NULL;

        surfaceDef = *surfaceYUVDef;
        hostSurfaceType = C2D_SURFACE_YUV_HOST;
    }
    else
    {
        C2D_RGB_SURFACE_DEF **surfaceRGBDef = (C2D_RGB_SURFACE_DEF **)
                                  (isSource ? &mSrcSurfaceDef : &mDstSurfaceDef);
        if (*surfaceRGBDef == NULL)
        {
            *surfaceRGBDef = (C2D_RGB_SURFACE_DEF *)
                                  calloc(1, sizeof(C2D_RGB_SURFACE_DEF));
            if (*surfaceRGBDef == NULL)
            {
                GST_ERROR("surfaceRGBDef allocation failed");
                return -1;
            }
        }
        else
        {
            memset(*surfaceRGBDef, 0, sizeof(C2D_RGB_SURFACE_DEF));
        }
        (*surfaceRGBDef)->format = getC2DFormat(format, isSource);
        (*surfaceRGBDef)->width = width;
        (*surfaceRGBDef)->height = height;
        (*surfaceRGBDef)->buffer = (void *)0xaaaaaaaa;
        (*surfaceRGBDef)->phys = (void *)0xaaaaaaaa;
        (*surfaceRGBDef)->stride = calcStride(format, width);

        surfaceDef = *surfaceRGBDef;
        hostSurfaceType = C2D_SURFACE_RGB_HOST;
    }

    ret = c2dCreateSurface(isSource ? &mSrcSurface :
                      &mDstSurface,
                      isSource ? C2D_SOURCE : C2D_TARGET,
                      (C2D_SURFACE_TYPE)(hostSurfaceType
                                         | C2D_SURFACE_WITH_PHYS
                                         | C2D_SURFACE_WITH_PHYS_DUMMY),
                      surfaceDef);
    return (int32_t) ret;
}

bool c2d_blend::isYUVSurface(ColorConvertFormat format)
{
    switch (format)
    {
        case NV12_128m:
            return true;
        default:
            return false;
    }
}

void * c2d_blend::mapGPUAddr(int bufFD, void *bufPtr, size_t bufLen)
{
    C2D_STATUS status;
    void *gpuaddr = NULL;
    status = c2dMapAddr(bufFD, bufPtr, bufLen, 0, KGSL_USER_MEM_TYPE_ION,
                         &gpuaddr);
    if (status != C2D_STATUS_OK)
    {
        GST_ERROR("c2dMapAddr failed: status %d fd %d ptr %p len %zu flags %s",
                status, bufFD, bufPtr, bufLen, "KGSL_USER_MEM_TYPE_ION");
        return NULL;
    }
    GST_DEBUG("c2d mapping created: gpuaddr %p fd %d ptr %p len %zu",
            gpuaddr, bufFD, bufPtr, bufLen);
    return gpuaddr;
}

bool c2d_blend::unmapGPUAddr(void *gAddr)
{

    C2D_STATUS status = c2dUnMapAddr(gAddr);

    if (status != C2D_STATUS_OK)
        GST_ERROR("c2dUnMapAddr failed: status %d gpuaddr %p", status, gAddr);

    return (status == C2D_STATUS_OK);
}

void c2d_blend::clearSurfaces()
{
    if (mSrcSurface)
    {
        c2dDestroySurface(mSrcSurface);
        mSrcSurface = 0;
    }
    if (mSrcSurfaceDef)
    {
        if (isYUVSurface(mSrcFormat))
        {
            delete ((C2D_YUV_SURFACE_DEF *)mSrcSurfaceDef);
        }
        else
        {
            delete ((C2D_RGB_SURFACE_DEF *)mSrcSurfaceDef);
        }
        mSrcSurfaceDef = NULL;
    }
    if (mDstSurface)
    {
        c2dDestroySurface(mDstSurface);
        mDstSurface = 0;
    }
    if (mDstSurfaceDef)
    {
        if (isYUVSurface(mDstFormat))
        {
            delete ((C2D_YUV_SURFACE_DEF *)mDstSurfaceDef);
        }
        else
        {
            delete ((C2D_RGB_SURFACE_DEF *)mDstSurfaceDef);
        }
        mDstSurfaceDef = NULL;
    }
}

const char *AdrenoLibLoader::mAdrenoUtilsLibName = "libadreno_utils.so";
