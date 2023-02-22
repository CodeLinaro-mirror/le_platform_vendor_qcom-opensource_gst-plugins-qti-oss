/*
* Copyright (c) 2021, The Linux Foundation. All rights reserved.
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
*/

#include "C2ComponentAdapter.h"

#include "C2WrapperUtils.h"

#include <chrono>
#include <C2PlatformSupport.h>
#include <gst/gst.h>
#include <C2AllocatorGBM.h>
#include <C2BlockInternal.h>
#ifdef USE_DMAHEAP
#include <C2DmaLinearAllocator.h>
#else
#include <C2AllocatorIon.h>
#include <C2HandleIonInternal.h>
#endif

GST_DEBUG_CATEGORY_EXTERN(gst_qcodec2_wrapper_debug);
#define GST_CAT_DEFAULT gst_qcodec2_wrapper_debug

/* Currently, size of input queue is 6 in video driver.
 * If count of pending works are more than 6, it causes queue overflow issue.
 */
#define MAX_PENDING_WORK 6
#define GBM_BO_USAGE_NV12_512_QTI 0x40000000

using namespace std::chrono_literals;

std::shared_ptr<C2Buffer> createLinearBuffer(const std::shared_ptr<C2LinearBlock>& block)
{
    return C2Buffer::CreateLinearBuffer(block->share(block->offset(), block->size(), ::C2Fence()));
}

std::shared_ptr<C2Buffer> createGraphicBuffer(const std::shared_ptr<C2GraphicBlock>& block)
{
    return C2Buffer::CreateGraphicBuffer(block->share(C2Rect(block->width(), block->height()), ::C2Fence()));
}

namespace QTI {

C2ComponentAdapter::C2ComponentAdapter(std::shared_ptr<C2Component> comp)
{

    LOG_MESSAGE("Component(%p) created", this);

    mComp = std::move(comp);
    mIntf = nullptr;
    mListener = nullptr;
    mCallback = nullptr;
    mLinearPool = nullptr;
    mGraphicPool = nullptr;
    mNumPendingWorks = 0;
    mDataCopyFunc = nullptr;
    mDataCopyFuncParam = nullptr;
    mC2AllocatorGBM = nullptr;
    mC2LinearAllocator = nullptr;
}

C2ComponentAdapter::~C2ComponentAdapter()
{

    LOG_MESSAGE("Component(%p) destroyed", this);

    mComp = nullptr;
    mIntf = nullptr;
    mListener = nullptr;
    mCallback = nullptr;
    mInPendingBuffer.clear();
    mOutPendingBuffer.clear();
    mTrackBuffers.clear();
    mLinearPool = nullptr;
    mGraphicPool = nullptr;
    mC2AllocatorGBM = nullptr;
    mC2LinearAllocator = nullptr;
}

c2_status_t C2ComponentAdapter::setListenercallback(std::unique_ptr<EventCallback> callback,
    c2_blocking_t mayBlock)
{

    LOG_MESSAGE("Component(%p) listener set", this);

    c2_status_t result = C2_NO_INIT;

    if (callback != NULL) {
        mListener = std::shared_ptr<C2Component::Listener>(new C2ComponentListenerAdapter(this));
        result = mComp->setListener_vb(mListener, mayBlock);
    }

    if (result == C2_OK) {
        mCallback = std::move(callback);
    }

    return result;
}

c2_status_t C2ComponentAdapter::setDataCopyFunc(void* func, void* param)
{
    c2_status_t result = C2_OK;
    mDataCopyFunc = reinterpret_cast<fnDataCopy>(func);
    mDataCopyFuncParam = param;

    return result;
}

c2_status_t C2ComponentAdapter::writePlane(uint8_t* dest, BufferDescriptor* buffer_info)
{
    c2_status_t result = C2_OK;
    uint8_t* dst = dest;
    uint8_t* src = buffer_info->data;

    if (dst == nullptr || src == nullptr) {
        LOG_ERROR("Inavlid buffer in writePlane(%p)", this);
        return C2_BAD_VALUE;
    }

    uint32_t width = buffer_info->width;
    uint32_t height = buffer_info->height;
    uint32_t stride = buffer_info->stride[0];

    LOG_MESSAGE("format %d, %ux%u, stride %u, "
                "offset %" G_GSIZE_FORMAT "-%" G_GSIZE_FORMAT,
        buffer_info->format, width, height, stride,
        buffer_info->offset[0], buffer_info->offset[1]);

    /*TODO: add support for other color formats */
    if (buffer_info->format == GST_VIDEO_FORMAT_NV12) {
        if (buffer_info->ubwc_flag) {
            memcpy(dst, src, buffer_info->size);
        } else {
            uint32_t y_stride = VENUS_Y_STRIDE(COLOR_FMT_NV12, width);
            uint32_t uv_stride = VENUS_UV_STRIDE(COLOR_FMT_NV12, width);
            uint32_t y_scanlines = VENUS_Y_SCANLINES(COLOR_FMT_NV12, height);

            src += buffer_info->offset[0];
            for (int i = 0; i < height; i++) {
                memcpy(dst, src, width);
                dst += y_stride;
                src += stride;
            }

            uint32_t offset = y_stride * y_scanlines;
            dst = dest + offset;
            if (buffer_info->offset[1] > 0) {
                src = buffer_info->data + buffer_info->offset[1];
            }

            for (int i = 0; i < height / 2; i++) {
                memcpy(dst, src, width);
                dst += uv_stride;
                src += stride;
            }
        }
    } else if (buffer_info->format == GST_VIDEO_FORMAT_P010_10LE) {
        uint32_t y_stride = VENUS_Y_STRIDE(COLOR_FMT_P010, width);
        uint32_t uv_stride = VENUS_UV_STRIDE(COLOR_FMT_P010, width);
        uint32_t y_scanlines = VENUS_Y_SCANLINES(COLOR_FMT_P010, height);

        src += buffer_info->offset[0];
        for (int i = 0; i < height; i++) {
            memcpy(dst, src, stride);
            dst += y_stride;
            src += stride;
        }

        uint32_t offset = y_stride * y_scanlines;
        dst = dest + offset;
        if (buffer_info->offset[1] > 0) {
            src = buffer_info->data + buffer_info->offset[1];
        }

        for (int i = 0; i < height / 2; i++) {
            memcpy(dst, src, stride);
            dst += uv_stride;
            src += stride;
        }
    } else if (buffer_info->format == GST_VIDEO_FORMAT_NV12_10LE32) {
        if (buffer_info->ubwc_flag) {
            memcpy(dst, src, buffer_info->size);
        } else {
            LOG_ERROR("Non UBWC NV12_10LE32 not supported yet");
            result = C2_BAD_VALUE;
        }
    } else {
        result = C2_BAD_VALUE;
    }

    return result;
}

c2_status_t C2ComponentAdapter::prepareC2Buffer(std::shared_ptr<C2Buffer>* c2Buf, BufferDescriptor* buffer)
{
    uint8_t* rawBuffer = buffer->data;
    uint8_t* destBuffer = nullptr;
    uint32_t frameSize = buffer->size;
    c2_status_t result = C2_OK;
    uint32_t allocSize = 0;

    if (rawBuffer == nullptr) {
        LOG_ERROR("Inavlid buffer in prepareC2Buffer(%p)", this);
        result = C2_BAD_VALUE;
    } else {
        std::shared_ptr<C2LinearBlock> linear_block;
        std::shared_ptr<C2GraphicBlock> graphic_block;

        std::shared_ptr<C2Buffer> buf;
        c2_status_t err = C2_OK;
        C2MemoryUsage usage = { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };
        if (buffer->secure) {
            usage = { C2MemoryUsage::READ_PROTECTED, 0 };
        }

        if (buffer->pool_type == BUFFER_POOL_BASIC_LINEAR) {
            allocSize = ALIGN(frameSize, 4096);
            err = mLinearPool->fetchLinearBlock(allocSize, usage, &linear_block);
            if (err != C2_OK || linear_block == nullptr) {
                LOG_ERROR("Linear pool failed to allocate input buffer of size : (%d)", frameSize);
                return C2_NO_MEMORY;
            }

            if (mDataCopyFunc) {
                if (linear_block->handle()) {
                    const C2Handle *handle = linear_block->handle();
                    if (!handle) {
                        LOG_ERROR("invalid C2 handle");
                        return C2_CORRUPTED;
                    }
                    uint32_t dest_fd = handle->data[0];
                    /* That data length is from upstream gst plugin pushed down gstbuffer.
                     * In the DataCopyFunc callback function, it may reduce the data length
                     * to its actual length accordingly, but couldn’t increase the length
                     * as the dst buffer is already allocated according to that data length.
                     * Hence, pass the data length pointer as parameter to DataCopyFunc
                     * so as to get the actual data length in return.
                     */
                    int ret = mDataCopyFunc(dest_fd, rawBuffer, &frameSize, mDataCopyFuncParam);
                    if (ret) {
                        LOG_ERROR("data copy failed");
                        return C2_CORRUPTED;
                    }

                    if (frameSize > buffer->size) {
                        LOG_ERROR("frameSize exceeds, previous: %u current: %u",
                            buffer->size, frameSize);
                        return C2_CORRUPTED;
                    }
                } else {
                    LOG_ERROR("invalid handle of linear block");
                    return C2_CORRUPTED;
                }
            } else {
                if (!buffer->secure) {
                    C2WriteView view = linear_block->map().get();
                    if (view.error() != C2_OK) {
                        LOG_ERROR("C2LinearBlock::map() failed : %d", view.error());
                        return C2_NO_MEMORY;
                    }
                    destBuffer = view.base();
                    memcpy(destBuffer, rawBuffer, frameSize);
                } else {
                    LOG_ERROR("should not be here for secure mode");
                    return C2_CORRUPTED;
                }
            }
            linear_block->mSize = frameSize;
            buf = createLinearBuffer(linear_block);
        } else if (buffer->pool_type == BUFFER_POOL_BASIC_GRAPHIC) {
            if (mGraphicPool) {
                if (buffer->format == GST_VIDEO_FORMAT_NV12) {
                    if (buffer->ubwc_flag) {
                        LOG_MESSAGE("NV12: usage add UBWC");
                        usage = {
                            C2MemoryUsage::CPU_READ | GBM_BO_USAGE_UBWC_ALIGNED_QTI,
                            C2MemoryUsage::CPU_WRITE
                        };
                    } else if (buffer->heic_flag) {
                        LOG_MESSAGE("NV12: usage add NV12 512 QTI");
                        usage = {
                            C2MemoryUsage::CPU_READ | GBM_BO_USAGE_NV12_512_QTI,
                            C2MemoryUsage::CPU_WRITE
                        };
                    }
                }

                err = mGraphicPool->fetchGraphicBlock(buffer->width, buffer->height,
                    gst_to_c2_gbmformat(buffer->format), usage, &graphic_block);
                C2GraphicView view(graphic_block->map().get());
                if (view.error() != C2_OK) {
                    LOG_ERROR("C2GraphicBlock::map failed: %d", view.error());
                    return C2_NO_MEMORY;
                }

                destBuffer = (guint8*)view.data()[0];

                if (C2_OK != writePlane(destBuffer, buffer)) {
                    LOG_ERROR("failed to write planes for graphic buffer");
                    return C2_NO_MEMORY;
                }

                buf = createGraphicBuffer(graphic_block);
                if (err != C2_OK || buf == nullptr) {
                    LOG_ERROR("Graphic pool failed to allocate input buffer");
                    return C2_NO_MEMORY;
                }
            }
        }

        *c2Buf = buf;
    }

    return result;
}

c2_status_t C2ComponentAdapter::waitForProgressOrStateChange(
    uint32_t maxPendingWorks, uint32_t timeoutMs)
{

    std::unique_lock<std::mutex> ul(mLock);
    LOG_MESSAGE("waitForProgressOrStateChange: pending = %u", mNumPendingWorks);

    if (mNumPendingWorks >= maxPendingWorks) {
        if (timeoutMs > 0) {
            if (mCondition.wait_for(ul, timeoutMs * 1ms) == std::cv_status::timeout) {
                LOG_ERROR("Timed-out waiting for work / state-transition (pending=%u)",
                    mNumPendingWorks);
                return C2_TIMED_OUT;
            } else {
                LOG_MESSAGE("wait done");
            }
        } else if (timeoutMs == 0) {
            mCondition.wait(ul);
        }
    }

    return C2_OK;
}

void C2ComponentAdapter::registerTrackBuffer(const C2FrameData& input)
{
    uint64_t frameIndex = input.ordinal.frameIndex.peeku();

    for (size_t i = 0; i < input.buffers.size(); ++i) {
        TrackBuffer* trackbuf = new TrackBuffer(this, frameIndex, input.buffers[i]);
        if (trackbuf != nullptr) {
            c2_status_t status = input.buffers[i]->registerOnDestroyNotify(
                onDestroyNotify, trackbuf);

            if (status != C2_OK) {
                LOG_ERROR("TrackBuffer registerOnDestroyNotify failed, buf idx:%zu", trackbuf->frameIndex);
                delete trackbuf;
            } else {
                LOG_MESSAGE("emplace buf idx:%zu TrackBuffer %p to mTrackBuffers", trackbuf->frameIndex, trackbuf);
                std::unique_lock<std::mutex> ul(mLock);
                mTrackBuffers.emplace(trackbuf);
            }
        }
    }
}

void C2ComponentAdapter::unregisterTrackBuffer(
    std::list<std::unique_ptr<C2Work> >& workItems)
{
    // Unregister input buffers onDestroyNotify
    for (const std::unique_ptr<C2Work>& work : workItems) {
        if (work) {

            uint64_t frameIndex = work->input.ordinal.frameIndex.peeku();

            {
                std::unique_lock<std::mutex> ul(mLock);
                for (auto it = mTrackBuffers.begin();
                     it != mTrackBuffers.end(); ++it) {
                    if ((*it)->frameIndex == frameIndex) {
                        if (auto buffer = (*it)->buffer.lock()) {
                            buffer->unregisterOnDestroyNotify(
                                onDestroyNotify, *it);
                        }

                        LOG_MESSAGE("erase buf idx:%zu, TrackBuffer %p",
                            frameIndex, (*it));
                        mTrackBuffers.erase(it);
                        delete (*it);
                    }
                }
            }
        }
    }
}

void C2ComponentAdapter::unregisterTrackBufferAll()
{
    LOG_MESSAGE("unregister all track buffers");

    std::unique_lock<std::mutex> ul(mLock);

    for (auto it = mTrackBuffers.begin(); it != mTrackBuffers.end(); ++it) {
        if (auto buf = (*it)->buffer.lock()) {
            LOG_MESSAGE("erase buf idx:%zu TrackBuffer %p", (*it)->frameIndex, (*it));
            buf->unregisterOnDestroyNotify(onDestroyNotify, *it);
        }
        delete (*it);
    }

    mTrackBuffers.clear();
}

void C2ComponentAdapter::onDestroyNotify(const C2Buffer* buf, void* arg)
{
    if (!buf || !arg) {
        LOG_MESSAGE("no buf");
        return;
    }

    TrackBuffer* trackbuf = (TrackBuffer*)arg;
    if (trackbuf->adapter) {
        trackbuf->adapter->onBufferDestroyed(buf, arg);
    }
}

void C2ComponentAdapter::onBufferDestroyed(const C2Buffer* buf, void* arg)
{
    std::unique_lock<std::mutex> ul(mLock);

    LOG_MESSAGE("%s mNumPendingWorks %d", __func__, mNumPendingWorks);

    TrackBuffer* trackbuf = (TrackBuffer*)arg;
    if (!mTrackBuffers.empty()) {

        auto buf = mTrackBuffers.find(trackbuf);
        if (buf != mTrackBuffers.end()) {
            LOG_MESSAGE("erase buf idx:%zu TrackBuffer %p", trackbuf->frameIndex, trackbuf);
            mTrackBuffers.erase(buf);
            delete trackbuf;
        }

        if (mNumPendingWorks > 0) {
            mNumPendingWorks--;
        }

        mCondition.notify_one();
    }
}

std::shared_ptr<C2Buffer> C2ComponentAdapter::alloc(BufferDescriptor* buffer)
{
    c2_status_t err = C2_OK;
    std::shared_ptr<C2Buffer> buf;
    gint32 fd = -1;
    guint32 size = 0;

    /* TODO: add support for linear buffer */
    if (buffer->pool_type == BUFFER_POOL_BASIC_GRAPHIC) {
        std::shared_ptr<C2GraphicBlock> graphicBlock;
        C2MemoryUsage usage = { C2MemoryUsage::CPU_READ, C2MemoryUsage::CPU_WRITE };

        if (mGraphicPool) {
            if (buffer->ubwc_flag) {
                usage = { C2MemoryUsage::CPU_READ | GBM_BO_USAGE_UBWC_ALIGNED_QTI, C2MemoryUsage::CPU_WRITE };
            }
            else if (buffer->heic_flag) {
                LOG_MESSAGE("NV12: usage add NV12 512 QTI");
                usage = { C2MemoryUsage::CPU_READ | GBM_BO_USAGE_NV12_512_QTI, C2MemoryUsage::CPU_WRITE };
            }
            err = mGraphicPool->fetchGraphicBlock(buffer->width, buffer->height,
                gst_to_c2_gbmformat(buffer->format), usage, &graphicBlock);
            C2GraphicView view(graphicBlock->map().get());
            if (view.error() != C2_OK) {
                LOG_ERROR("C2GraphicBlock::map failed: %d", view.error());
                return NULL;
            }
            buf = createGraphicBuffer(graphicBlock);
            if (err != C2_OK || buf == nullptr) {
                LOG_ERROR("Graphic pool failed to allocate input buffer");
                return NULL;
            } else {
                const C2Handle* handle = graphicBlock->handle();
                if (nullptr == handle) {
                    LOG_ERROR("C2GraphicBlock handle is null");
                    return NULL;
                }

                /* ref the buffer and store it. When the fd is queued,
                 * we can find the graphic block with the input fd */
                fd = handle->data[0];
                mInPendingBuffer[fd] = graphicBlock;
                buffer->fd = fd;

                guint32 stride = 0;
                guint32 height = 0;
                guint32 format = 0;
                guint64 usage = 0;

                _UnwrapNativeCodec2GBMMetadata(handle, nullptr,
                    &height, &format, &usage, &stride, &size, nullptr);
                buffer->capacity = size;
                uint32_t y_scanlines = VENUS_Y_SCANLINES(
                    gbmformat_to_colorformat(format, usage), height);
                buffer->stride[0] = buffer->stride[1] = stride;
                buffer->offset[0] = 0;
                buffer->offset[1] = stride * y_scanlines;

                LOG_MESSAGE("allocated C2Buffer, fd: %d capacity: %d, ubwc: %d,"
                            " stride %u, offset %" G_GSIZE_FORMAT,
                    fd, buffer->capacity, buffer->ubwc_flag,
                    stride, buffer->offset[1]);
            }
        } else {
            LOG_ERROR("Graphic pool is not created");
            return NULL;
        }
    } else {
        LOG_ERROR("Unsupported pool type: %u", buffer->pool_type);
        return NULL;
    }

    return buf;
}

c2_status_t C2ComponentAdapter::queue(BufferDescriptor* buffer)
{
    uint8_t* inputBuffer = buffer->data;
    gint32 fd = buffer->fd;
    C2FrameData::flags_t inputFrameFlag = toC2Flag(buffer->flag);
    uint64_t frame_index = buffer->index;
    uint64_t timestamp = buffer->timestamp;

    LOG_MESSAGE("Component(%p) work queued, Frame index : %lu, Timestamp : %lu",
        this, frame_index, timestamp);

    c2_status_t result = C2_OK;
    std::list<std::unique_ptr<C2Work> > workList;
    std::unique_ptr<C2Work> work = std::make_unique<C2Work>();
    std::shared_ptr<C2Buffer> c2Buffer;

    work->input.flags = inputFrameFlag;
    work->input.ordinal.timestamp = timestamp;
    work->input.ordinal.frameIndex = frame_index;
    bool isEOSFrame = inputFrameFlag & C2FrameData::FLAG_END_OF_STREAM;

    work->input.buffers.clear();

    /* check if input buffer contains fd/va and decide if we need to
     * allocate a new C2 buffer or not */
    if (buffer->c2Buffer) {
        /* Disable delete function for this shared_ptr to avoid double free issue
         * since it is created from raw pointer got from another shared_ptr. That
         * shared_ptr takes responsibility to call delete function.*/
        std::shared_ptr<C2Buffer> c2Buffer(static_cast<C2Buffer*>(buffer->c2Buffer), [](C2Buffer*) {});
        work->input.buffers.emplace_back(c2Buffer);
    } else if (fd > 0) {
        if (buffer->pool_type == BUFFER_POOL_BASIC_LINEAR) {
            /* If the buffer fd is positive, we assume it is a valid external
             * dma buffer, then will try to import the external buffer by fd */
            std::shared_ptr<C2Buffer> clientBuf = nullptr;
            result = importExternalBuf (clientBuf, fd, buffer->size);
            if (result == C2_OK) {
                work->input.buffers.emplace_back (clientBuf);
            } else {
                LOG_ERROR("Failed(%d) to import buffer", result);
            }
        } else if (buffer->pool_type == BUFFER_POOL_BASIC_GRAPHIC) {
            std::map<uint64_t, std::shared_ptr<C2GraphicBlock> >::iterator it;
            std::shared_ptr<C2Buffer> buf = nullptr;
            std::shared_ptr<C2GraphicBlock> graphicBlock = nullptr;

            /* Find the buffer with fd */
            it = mInPendingBuffer.find(fd);
            if (it != mInPendingBuffer.end()) {
                graphicBlock = it->second;
                if (graphicBlock) {
                    buf = createGraphicBuffer(graphicBlock);
                    work->input.buffers.emplace_back(buf);
                } else {
                    LOG_ERROR("invalid graphic block");
                    result = C2_NO_MEMORY;
                }
            } else {
                /* If the buffer is not found, we assume it is a valid external buffer.
                 * When using external buffer, first attach the fd to C2AllocatorGBM,
                 * then when calling alloc(), it will try to import the external
                 * buffer by fd instead of allocating a new one. */
                if (!isUseExternalBuffer(BUFFER_POOL_BASIC_GRAPHIC)) {
                    setUseExternalBuffer(BUFFER_POOL_BASIC_GRAPHIC, TRUE);
                    LOG_MESSAGE("Set to use external buffer for C2AllocatorGBM");
                }
                result = attachExternalFd(BUFFER_POOL_BASIC_GRAPHIC, fd);
                if (result == C2_OK) {
                    buf = alloc(buffer);
                    if (buf) {
                        work->input.buffers.emplace_back(buf);
                        LOG_MESSAGE("Successfully import and queue the external "
                            "buffer, fd=%d", fd);
                    } else {
                        LOG_ERROR("Failed to import external fd: %d", fd);
                        result = C2_CORRUPTED;
                    }
                } else {
                    LOG_ERROR("Failed(%d) to attach external fd: %d", result, fd);
                }
            }
        } else {
            LOG_ERROR("Invalid buffer pool type %d", buffer->pool_type);
        }
    } else if (inputBuffer) {
        std::shared_ptr<C2Buffer> clientBuf;

        result = prepareC2Buffer(&clientBuf, buffer);
        if (result == C2_OK) {
            work->input.buffers.emplace_back(clientBuf);
        } else {
            LOG_ERROR("Failed(%d) to allocate buffer", result);
            result = C2_NO_MEMORY;
        }
    } else if (isEOSFrame) {
        LOG_MESSAGE("queue EOS frame");
    } else {
        LOG_ERROR("invalid buffer decriptor");
        result = C2_BAD_VALUE;
    }

    if (result == C2_OK) {
        registerTrackBuffer(work->input);

        work->worklets.clear();
        work->worklets.emplace_back(new C2Worklet);
        workList.push_back(std::move(work));

        if (!isEOSFrame) {
            waitForProgressOrStateChange(MAX_PENDING_WORK, 0);
        } else {
            LOG_MESSAGE("EOS reached");
        }

        result = mComp->queue_nb(&workList);
        if (result != C2_OK) {
            LOG_ERROR("Failed to queue work");
        } else {
            std::unique_lock<std::mutex> ul(mLock);
            mNumPendingWorks++;
        }
    }

    return result;
}

c2_status_t C2ComponentAdapter::flush(C2Component::flush_mode_t mode)
{
    c2_status_t result = C2_OK;
    std::list<std::unique_ptr<C2Work> > flushedWork;

    result = mComp->flush_sm(mode, &flushedWork);
    if (result == C2_OK) {
        LOG_MESSAGE("Component(%p) flushed work num:%zu", this, flushedWork.size());
        unregisterTrackBuffer(flushedWork);
    } else {
        LOG_ERROR("Failed to flush work");
    }

    return result;
}

c2_status_t C2ComponentAdapter::drain(C2Component::drain_mode_t mode)
{

    LOG_MESSAGE("Component(%p) drain", this);

    c2_status_t result = C2_OK;
    UNUSED(mode);

    return result;
}

c2_status_t C2ComponentAdapter::start()
{

    LOG_MESSAGE("Component(%p) start", this);

    return mComp->start();
}

c2_status_t C2ComponentAdapter::stop()
{

    LOG_MESSAGE("Component(%p) stop", this);

    c2_status_t result = mComp->stop();

    unregisterTrackBufferAll();

    return result;
}

c2_status_t C2ComponentAdapter::reset()
{

    LOG_MESSAGE("Component(%p) reset", this);

    c2_status_t result = mComp->reset();

    unregisterTrackBufferAll();

    return result;
}

c2_status_t C2ComponentAdapter::release()
{

    LOG_MESSAGE("Component(%p) release", this);

    c2_status_t result = mComp->release();

    unregisterTrackBufferAll();

    return result;
}

C2ComponentInterfaceAdapter* C2ComponentAdapter::intf()
{

    LOG_MESSAGE("Component(%p) interface created", this);

    if (mComp) {
        std::shared_ptr<C2ComponentInterface> compIntf = nullptr;

        compIntf = mComp->intf();
        mIntf = std::shared_ptr<C2ComponentInterfaceAdapter>(new C2ComponentInterfaceAdapter(compIntf));
    }

    return (mIntf == NULL) ? NULL : mIntf.get();
}

c2_status_t C2ComponentAdapter::createBlockpool(C2BlockPool::local_id_t poolType)
{

    LOG_MESSAGE("Component(%p) block pool (%lu) allocated", this, poolType);

    std::shared_ptr<C2BlockPool> pool;
    std::shared_ptr<C2Allocator> allocator;
    c2_status_t ret = C2_OK;

    if (poolType == C2BlockPool::BASIC_LINEAR) {
        ret = android::CreateCodec2BlockPool(C2AllocatorStore::DEFAULT_LINEAR, mComp, &mLinearPool);
        if (ret != C2_OK || mLinearPool == nullptr) {
            return ret;
        }
        uint64_t local_id = mLinearPool->getLocalId();
        android::GetCodec2BlockPoolWithAllocator(local_id, mComp, &pool, &allocator);
        if (allocator == nullptr) {
            LOG_ERROR("Failed to get allocator");
            ret = C2_NOT_FOUND;
        } else {
            mC2LinearAllocator = allocator;
        }
    } else if (poolType == C2BlockPool::BASIC_GRAPHIC) {
        ret = android::CreateCodec2BlockPool(C2AllocatorStore::DEFAULT_GRAPHIC, mComp, &mGraphicPool);
        if (ret != C2_OK || mGraphicPool == nullptr) {
            return ret;
        }
        uint64_t local_id = mGraphicPool->getLocalId();
        android::GetCodec2BlockPoolWithAllocator(local_id, mComp, &pool, &allocator);
        if (allocator == nullptr) {
            LOG_ERROR("Failed to get allocator");
            ret = C2_NOT_FOUND;
        } else {
            mC2AllocatorGBM = std::dynamic_pointer_cast<android::C2AllocatorGBM>(allocator);
            auto func = std::bind(&C2ComponentAdapter::acquireExtBuf, this,
                                  std::placeholders::_1, std::placeholders::_2);
            if (mC2AllocatorGBM) {
                mC2AllocatorGBM->setAcquireExtBufCb(func);
            }
        }
    }

    if (ret != C2_OK) {
        LOG_ERROR("Failed (%d) to create block pool (%lu)", ret, poolType);
    }

    return ret;
}

c2_status_t C2ComponentAdapter::configBlockPool(C2BlockPool::local_id_t poolType)
{
    C2BlockPool::local_id_t local_id;
    c2_status_t ret = C2_OK;

    LOG_MESSAGE("Component(%p) config block pool (%lu)", this, poolType);

    local_id = (poolType == C2BlockPool::BASIC_GRAPHIC) ? mGraphicPool->getLocalId() : mLinearPool->getLocalId();
    LOG_MESSAGE("Get pool local id:%lu", local_id);
    std::vector<C2Param*> params;
    std::unique_ptr<C2PortBlockPoolsTuning::output> pool = C2PortBlockPoolsTuning::output::AllocUnique({ local_id });
    params.push_back(pool.get());
    ret = mIntf->config(params, C2_DONT_BLOCK);
    if (ret != C2_OK) {
        LOG_ERROR("Failed (%d) to config block pool (%lu)", ret, poolType);
    }

    return ret;
}

uint32_t C2ComponentAdapter::getInterlaceMode(std::vector<std::unique_ptr<C2Param> >& configUpdate)
{
    uint32_t interlace = INTERLACE_MODE_PROGRESSIVE;
    android::ReflectedParamUpdater::Dict paramsMap;
    android::ReflectedParamUpdater::Value paramVal;
    C2Value c2Value;

    paramsMap = mIntf->getParams(configUpdate);
    if (paramsMap.find("vendor.qti-ext-dec-info-interlace.format") != paramsMap.end()) {
        paramVal = paramsMap["vendor.qti-ext-dec-info-interlace.format"];
        if (paramVal.find(&c2Value)) {
            if (c2Value.get(&interlace)) {
                LOG_DEBUG("interlace type:%u", interlace);
            }
        }
    }

    return interlace;
}

void C2ComponentAdapter::handleWorkDone(
    std::weak_ptr<C2Component> component,
    std::list<std::unique_ptr<C2Work> > workItems)
{

    LOG_MESSAGE("Component(%p) work done", this);

    while (!workItems.empty()) {
        std::unique_ptr<C2Work> work = std::move(workItems.front());

        workItems.pop_front();
        if (!work) {
            continue;
        }

        if (work->worklets.empty()) {
            LOG_DEBUG("Component(%p) worklet empty", this);
            continue;
        }

        if (work->result != C2_OK) {
            LOG_DEBUG("No output for component(%p), ret:%d", this, work->result);
            continue;
        }

        const std::unique_ptr<C2Worklet>& worklet = work->worklets.front();
        std::shared_ptr<C2Buffer> buffer = nullptr;
        uint64_t bufferIdx = 0;
        C2FrameData::flags_t outputFrameFlag = worklet->output.flags;
        uint64_t timestamp = worklet->output.ordinal.timestamp.peeku();
        uint32_t interlace = getInterlaceMode(worklet->output.configUpdate);

        while (!worklet->output.configUpdate.empty()) {
            std::unique_ptr<C2Param> param;
            worklet->output.configUpdate.back().swap(param);
            worklet->output.configUpdate.pop_back();
            switch (param->coreIndex().coreIndex()) {
            case C2PortActualDelayTuning::CORE_INDEX: {
                if (param->forOutput()) {
                    C2PortActualDelayTuning::output outputDelay;
                    if (outputDelay.updateFrom(*param)) {
                        if (mC2AllocatorGBM) {
                            LOG_MESSAGE("onWorkDone: updating output delay:%u local_id:%lu",
                                outputDelay.value, mGraphicPool->getLocalId());
                            if (isUseExternalBuffer(BUFFER_POOL_BASIC_GRAPHIC)) {
                                /* Update the max acquirable buffer count for external buffer pool */
                                mCallback->onUpdateMaxBufCount(outputDelay.value);
                            } else {
                                mC2AllocatorGBM->setMaxAllocationCount(outputDelay.value);
                            }
                        } else {
                            LOG_ERROR("mC2AllocatorGBM is NULL");
                        }
                    }
                }
            } break;
            }
        }

        // Expected only one output stream.
        if (worklet->output.buffers.size() == 1u) {
            buffer = worklet->output.buffers[0];
            bufferIdx = worklet->output.ordinal.frameIndex.peeku();
            if (!buffer) {
                LOG_ERROR("Invalid buffer");
            }

            LOG_MESSAGE("Component(%p) output buffer available, Frame index : %lu, Timestamp : %lu, flag: %x",
                this, bufferIdx, worklet->output.ordinal.timestamp.peeku(), outputFrameFlag);

            // ref count ++
            {
                std::unique_lock<std::mutex> lck(mLockOut);
                mOutPendingBuffer[bufferIdx] = buffer;
            }

            mCallback->onOutputBufferAvailable(buffer, bufferIdx, timestamp, interlace, outputFrameFlag);
        } else {
            if (outputFrameFlag & C2FrameData::FLAG_END_OF_STREAM) {
                LOG_MESSAGE("Component(%p) reached EOS on output", this);
                mCallback->onOutputBufferAvailable(NULL, bufferIdx, timestamp, interlace, outputFrameFlag);
            } else if (outputFrameFlag & C2FrameData::FLAG_INCOMPLETE) {
                LOG_MESSAGE("Component(%p) work incomplete, means an input frame results in multiple "
                            "output frames, or codec config update event",
                    this);
                continue;
            } else {
                LOG_MESSAGE("Incorrect number of output buffers: %lu", worklet->output.buffers.size());
            }

            break;
        }
    }
}

void C2ComponentAdapter::handleTripped(
    std::weak_ptr<C2Component> component,
    std::vector<std::shared_ptr<C2SettingResult> > settingResult)
{

    LOG_MESSAGE("Component(%p) work tripped", this);

    UNUSED(component);

    for (auto& f : settingResult) {
        mCallback->onTripped(static_cast<uint32_t>(f->failure));
    }
}

void C2ComponentAdapter::handleError(std::weak_ptr<C2Component> component, uint32_t errorCode)
{
    LOG_MESSAGE("Component(%p) posts an error", this);

    UNUSED(component);
    mCallback->onError(errorCode);
}

c2_status_t C2ComponentAdapter::setCompStore(std::weak_ptr<C2ComponentStore> store)
{

    LOG_MESSAGE("Component store for component(%p) set", this);

    c2_status_t result = C2_BAD_VALUE;
    if (!store.expired()) {
        mStore = store;
        result = C2_OK;
    }
    return result;
}

c2_status_t C2ComponentAdapter::freeOutputBuffer(uint64_t bufferIdx)
{

    LOG_MESSAGE("Freeing component(%p) output buffer(%lu)", this, bufferIdx);

    c2_status_t result = C2_BAD_VALUE;
    std::map<uint64_t, std::shared_ptr<C2Buffer> >::iterator it;

    {
        std::unique_lock<std::mutex> lck(mLockOut);
        it = mOutPendingBuffer.find(bufferIdx);
        if (it != mOutPendingBuffer.end()) {
            mOutPendingBuffer.erase(it);
            result = C2_OK;

        } else {
            LOG_ERROR("Buffer index(%lu) not found", bufferIdx);
        }
    }

    return result;
}

c2_status_t C2ComponentAdapter::attachExternalFd(BUFFER_POOL_TYPE type, int fd)
{
    c2_status_t result = C2_NO_INIT;
    LOG_MESSAGE("Component(%p) attach external fd: %d for pool type %d", this, fd, type);

    if (type == BUFFER_POOL_BASIC_GRAPHIC) {
        if (mC2AllocatorGBM) {
            result = mC2AllocatorGBM->attachExternalFd(fd);
        } else {
            LOG_ERROR("mC2AllocatorGBM is NULL");
            result = C2_BAD_VALUE;
        }
    } else {
        LOG_ERROR("Invalid buffer pool type %d", type);
    }

    if (C2_OK != result) {
        LOG_ERROR("Failed to attach external fd with result=%d", result);
    }

    return result;
}

c2_status_t C2ComponentAdapter::setUseExternalBuffer(BUFFER_POOL_TYPE type, bool useExternal)
{
    c2_status_t result = C2_NO_INIT;
    LOG_MESSAGE("Component(%p) set to use external buffer: %s for pool type %d",
        this, useExternal ? "TRUE" : "FALSE", type);

    if (type == BUFFER_POOL_BASIC_GRAPHIC) {
        if (mC2AllocatorGBM) {
            result = mC2AllocatorGBM->setUseExternalBuffer(useExternal);
        } else {
            LOG_ERROR("mC2AllocatorGBM is NULL");
            result = C2_BAD_VALUE;
        }
    } else {
        LOG_ERROR("Invalid buffer pool type %d", type);
    }

    return result;
}

bool C2ComponentAdapter::isUseExternalBuffer(BUFFER_POOL_TYPE type)
{
    bool ret = false;

    if (type == BUFFER_POOL_BASIC_GRAPHIC) {
        if (mC2AllocatorGBM) {
            ret = mC2AllocatorGBM->isUseExternalBuffer();
        } else {
            LOG_ERROR("mC2AllocatorGBM is NULL");
        }
    } else {
        LOG_ERROR("Invalid buffer pool type %d", type);
    }

    return ret;
}

c2_status_t C2ComponentAdapter::importExternalBuf(std::shared_ptr<C2Buffer>& c2Buf, int fd, uint32_t size)
{
    c2_status_t result = C2_OK;
    std::shared_ptr<C2LinearBlock> linearBlock = nullptr;
    std::shared_ptr<C2LinearAllocation> allocation = nullptr;
    bool need_release = false;
    C2Handle *linearHandle = nullptr;

    uint32_t alignSize = ALIGN (size, 4096);
    /* dup the external buffer fd to decouple decoder and upstream element, and the
     * input external buffer fd should be closed by upstream element after use, dup_fd
     * will be closed in the destructor of C2AllocationIon::Impl after passing to it */
    int dup_fd = dup(fd);

#ifdef USE_DMAHEAP
    linearHandle = new android::C2DmaHandle (dup_fd, alignSize);
#else
    linearHandle = new android::C2HandleIon (dup_fd, alignSize);
#endif

    if (nullptr == mC2LinearAllocator || nullptr == linearHandle) {
        LOG_ERROR ("Invalid mC2LinearAllocator or linearHandle");
        need_release = true;
        close(dup_fd);
        result = C2_NO_MEMORY;
        goto do_exit;
    }
    /* linearHandle will be released in priorLinearAllocation if return C2_OK */
    result = mC2LinearAllocator->priorLinearAllocation (linearHandle, &allocation);
    if (result != C2_OK) {
        LOG_ERROR ("Failed(%d) to call priorLinearAllocation", result);
        need_release = true;
        goto do_exit;
    }
    linearBlock = _C2BlockFactory::CreateLinearBlock (allocation);
    if (linearBlock == nullptr) {
        LOG_ERROR ("Failed to CreateLinearBlock");
        result = C2_NO_MEMORY;
        goto do_exit;
    }
    linearBlock->mSize = size;
    c2Buf = createLinearBuffer (linearBlock);
    if (!c2Buf) {
        LOG_ERROR ("Failed to createLinearBuffer");
        result = C2_NO_MEMORY;
    }

do_exit:
    if (need_release && linearHandle) {
        /* need to delete linearHandle here if priorLinearAllocation failed */
        delete linearHandle;
    }

    return result;
}

void C2ComponentAdapter::acquireExtBuf(uint32_t width, uint32_t height)
{
    mCallback->onAcquireExtBuffer(width, height);
}

C2ComponentListenerAdapter::C2ComponentListenerAdapter(C2ComponentAdapter* comp)
{

    mComp = comp;
}

C2ComponentListenerAdapter::~C2ComponentListenerAdapter()
{

    mComp = nullptr;
}

void C2ComponentListenerAdapter::onWorkDone_nb(
    std::weak_ptr<C2Component> component,
    std::list<std::unique_ptr<C2Work> > workItems)
{

    LOG_MESSAGE("Component listener (%p) onWorkDone_nb", this);

    if (mComp) {
        mComp->handleWorkDone(component, std::move(workItems));
    }
}

void C2ComponentListenerAdapter::onTripped_nb(
    std::weak_ptr<C2Component> component,
    std::vector<std::shared_ptr<C2SettingResult> > settingResult)
{

    LOG_MESSAGE("Component listener (%p) onTripped_nb", this);

    if (mComp) {
        mComp->handleTripped(component, settingResult);
    }
}

void C2ComponentListenerAdapter::onError_nb(std::weak_ptr<C2Component> component, uint32_t errorCode)
{

    LOG_MESSAGE("Component listener (%p) onError_nb", this);

    if (mComp) {
        mComp->handleError(component, errorCode);
    }
}

} // namespace QTI
