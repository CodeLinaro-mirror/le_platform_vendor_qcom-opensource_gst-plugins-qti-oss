// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <dlfcn.h>
#include "gstvesdeliverallocator.h"

/* Dynamically load libs by dlopen. */
#ifdef USE_DMAHEAP
static const char *lib_name = "libdmabufheap.so.0";
#else
static const char *lib_name = "libion.so.0";
#endif

#if CONTIGUOUS_MEM_OPTION == 0
static const char *contiguous_mem_heap_name = "qcom,display";
#elif CONTIGUOUS_MEM_OPTION == 1
static const char *contiguous_mem_heap_name = "system-secure";
#endif

GST_DEBUG_CATEGORY_EXTERN (vesdeliver_debug);
#define GST_CAT_DEFAULT vesdeliver_debug

#define gst_vesdeliver_allocator_parent_class parent_class
G_DEFINE_TYPE (GstVesDeliverAllocator, gst_vesdeliver_allocator,
    GST_TYPE_DMABUF_ALLOCATOR);

#define ALIGN(num, to) (((num) + (to - 1)) & (~(to - 1)))
#define ALLOC_BUFFER_COUNT_INCREMENT 10
#define SHARED_BUF_WAIT_TIMEOUT_MS 100

static void
gst_vesdeliver_allocator_init (GstVesDeliverAllocator * alloc)
{
  GstAllocator *allocator = GST_ALLOCATOR_CAST (alloc);
  allocator->mem_type = GST_ALLOCATOR_VESDELIVER;
  GST_OBJECT_FLAG_SET (alloc, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);

  g_mutex_init (&alloc->buf_lock);
  g_cond_init (&alloc->buf_cond);
  alloc->buffer_list = NULL;
  alloc->max_alloc_buf_size = 0;
  alloc->lib_handle = dlopen (lib_name, RTLD_NOW);
  if (NULL == alloc->lib_handle) {
    const char *dlerr = dlerror ();
    if (NULL == dlerr)
      dlerr = "NULL";
    GST_ERROR_OBJECT (alloc, "dlopen %s error: %s", lib_name, dlerr);
    return;
  }
#ifdef USE_DMAHEAP
  alloc->create_allocator =
      dlsym (alloc->lib_handle, "CreateDmabufHeapBufferAllocator");
  alloc->free_allocator =
      dlsym (alloc->lib_handle, "FreeDmabufHeapBufferAllocator");
  alloc->alloc_fd = dlsym (alloc->lib_handle, "DmabufHeapAlloc");

  if (!alloc->create_allocator || !alloc->free_allocator || !alloc->alloc_fd) {
    GST_ERROR_OBJECT
        (alloc, "dlsym failed with create_allocator: %p, free_allocator: %p, alloc_fd: %p",
        alloc->create_allocator, alloc->free_allocator, alloc->alloc_fd);
    dlclose (alloc->lib_handle);
    alloc->lib_handle = NULL;
    return;
  } else {
    GST_INFO_OBJECT (alloc, "open %s(%p) successfully", lib_name,
        alloc->lib_handle);
  }

  alloc->dmaheap_allocator = alloc->create_allocator ();
  if (!alloc->dmaheap_allocator) {
    GST_ERROR_OBJECT (alloc, "Failed to create dma heap allocator");
    return;
  }
#else
  int ion_dlsym_fail = 0;
  alloc->ion_open = dlsym (alloc->lib_handle, "ion_open");
  alloc->ion_close = dlsym (alloc->lib_handle, "ion_close");
  alloc->ion_alloc_fd = dlsym (alloc->lib_handle, "ion_alloc_fd");
#ifdef ION_FLAG_ION_LEND_BUF
  alloc->ion_lend_buf = dlsym (alloc->lib_handle, "ion_lend_buf");
  alloc->ion_reclaim_buf = dlsym (alloc->lib_handle, "ion_reclaim_buf");

  if (!alloc->ion_open || !alloc->ion_close || !alloc->ion_alloc_fd || !alloc->ion_lend_buf || !alloc->ion_reclaim_buf) {
    ion_dlsym_fail = 1;
    GST_ERROR_OBJECT
        (alloc, "dlsym failed with ion_open: %p, ion_close: %p, ion_alloc_fd: %p, ion_lend_buf: %p, ion_reclaim_buf: %p",
        alloc->ion_open, alloc->ion_close, alloc->ion_alloc_fd, alloc->ion_lend_buf, alloc->ion_reclaim_buf);
  }
#else
  alloc->ion_lend_buf = NULL;
  alloc->ion_reclaim_buf = NULL;

  if (!alloc->ion_open || !alloc->ion_close || !alloc->ion_alloc_fd) {
    ion_dlsym_fail = 1;
    GST_ERROR_OBJECT
        (alloc, "dlsym failed with ion_open: %p, ion_close: %p, ion_alloc_fd: %p",
        alloc->ion_open, alloc->ion_close, alloc->ion_alloc_fd);
  }
#endif
  if (ion_dlsym_fail) {
    dlclose (alloc->lib_handle);
    alloc->lib_handle = NULL;
    return;
  } else {
    GST_INFO_OBJECT (alloc, "open %s(%p) successfully", lib_name,
        alloc->lib_handle);
  }

  alloc->ion_fd = alloc->ion_open ();
  if (alloc->ion_fd < 0) {
    GST_ERROR_OBJECT (alloc, "Open ION device failed with %d", alloc->ion_fd);
    return;
  }
#endif
}

GstAllocator *
gst_vesdeliver_allocator_new (AllocatorParameter * param)
{
  GstVesDeliverAllocator *allocator = NULL;

  allocator =
      (GstVesDeliverAllocator *) g_object_new (GST_TYPE_VESDELIVER_ALLOCATOR,
      NULL);
  if (NULL == allocator) {
    GST_ERROR ("Failed to create vesdeliver allocator");
    return NULL;
  }
  allocator->param = *param;

  GST_INFO_OBJECT (allocator,
      "Create vesdeliver allocator %p with buffer recycle %s in secure mode %d",
      allocator, allocator->param.buf_recycle ? "enabled" : "disabled",
      allocator->param.secure_mode);

  if (allocator->param.secure_mode == LEND_DMABUF
      && allocator->param.buf_contiguous) {
    GST_INFO_OBJECT (allocator,
        "Physical contiguous memory will be allocated in lend dmabuf mode");
  }
  return GST_ALLOCATOR_CAST (allocator);
}

static void
_free_buffer (gpointer data, gpointer user_data)
{
  BitstreamBuffer *buffer = (BitstreamBuffer *) data;
  GstAllocator *allocator = GST_ALLOCATOR_CAST (user_data);

  if (buffer) {
    GST_DEBUG ("free the buffer with fd=%d", buffer->fd);
    if (buffer->mem) {
      GST_ALLOCATOR_CLASS (parent_class)->free (allocator, buffer->mem);
      buffer->mem = NULL;
    }
    g_free (buffer);
    buffer = NULL;
  }
}

void
gst_vesdeliver_allocator_finalize (GObject * object)
{
  GstVesDeliverAllocator *allocator = GST_VESDELIVER_ALLOCATOR_CAST (object);

  g_mutex_clear (&allocator->buf_lock);
  g_cond_clear (&allocator->buf_cond);
  if (allocator->buffer_list) {
    g_slist_foreach (allocator->buffer_list, _free_buffer, allocator);
    g_slist_free (allocator->buffer_list);
    allocator->buffer_list = NULL;
  }
#ifdef USE_DMAHEAP
  if (allocator->dmaheap_allocator) {
    allocator->free_allocator (allocator->dmaheap_allocator);
    allocator->dmaheap_allocator = NULL;
  }
#else
  if (allocator->ion_fd > 0) {
    allocator->ion_close (allocator->ion_fd);
    allocator->ion_fd = -1;
  }
#endif

  if (allocator->lib_handle) {
    GST_INFO_OBJECT (allocator, "dlclose %s(%p)", lib_name,
        allocator->lib_handle);
    dlclose (allocator->lib_handle);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstMemory *
_acquire_buffer (GstAllocator * allocator, gsize alloc_size)
{
  GstVesDeliverAllocator *alloc = GST_VESDELIVER_ALLOCATOR (allocator);
  GstMemory *mem = NULL;
  gint64 timeout = 0;
  guint list_size = 0;
  GSList *iter = NULL;
  gboolean acquired = FALSE;

  list_size = g_slist_length (alloc->buffer_list);
  GST_DEBUG_OBJECT (alloc, "the size of buffer list is %u", list_size);

  /* check the buffer list to see if any buffers are available */
  while ((alloc->max_alloc_buf_size >= alloc_size) && !acquired) {
    g_mutex_lock (&alloc->buf_lock);
    for (iter = alloc->buffer_list; iter; iter = iter->next) {
      BitstreamBuffer *buf = (BitstreamBuffer *) iter->data;
      if (buf && !buf->used && buf->size >= alloc_size) {
        GST_DEBUG_OBJECT (alloc,
            "Found available buffer in list with fd=%d size=%lu mem=%p",
            buf->fd, buf->size, buf->mem);
        buf->used = TRUE;
        g_object_ref (buf->mem->allocator);
        mem = gst_memory_ref (buf->mem);
        acquired = TRUE;
        break;
      }
    }

    if (!acquired) {
      if (list_size > alloc->param.threshold_buf_count) {
        timeout =
            g_get_monotonic_time () +
            (SHARED_BUF_WAIT_TIMEOUT_MS * G_TIME_SPAN_MILLISECOND);
        if (!g_cond_wait_until (&alloc->buf_cond, &alloc->buf_lock, timeout)) {
          GST_WARNING_OBJECT (alloc, "wait for shared buffer timeout!");
          g_mutex_unlock (&alloc->buf_lock);
          break;
        } else {
          GST_DEBUG_OBJECT (alloc,
              "one shared buffer can be reused, retry acquiring");
        }
      } else {
        g_mutex_unlock (&alloc->buf_lock);
        break;
      }
    }
    g_mutex_unlock (&alloc->buf_lock);
  }

  return mem;
}

static gint
_buf_size_cmp_func (gconstpointer a, gconstpointer b)
{
  BitstreamBuffer *buf_a = (BitstreamBuffer *) a;
  BitstreamBuffer *buf_b = (BitstreamBuffer *) b;

  return buf_a->size - buf_b->size;
}

static void
_try_remove_buffer_from_list (GstAllocator * allocator)
{
  GstVesDeliverAllocator *alloc = GST_VESDELIVER_ALLOCATOR (allocator);
  BitstreamBuffer *buf = NULL;
  GSList *iter = NULL;
  gint idx = 0;

  for (iter = alloc->buffer_list; iter; iter = iter->next, idx++) {
    buf = (BitstreamBuffer *) iter->data;
    if (buf && (buf->used == FALSE)) {
      GST_INFO_OBJECT (alloc, "Find the min unused buffer at index %d", idx);
      break;
    }
  }
  // will not remove the last max buffer
  if (iter != NULL && iter->next != NULL && buf) {
    GST_INFO_OBJECT (alloc,
        "remove the min unused buffer with fd=%d size=%"
        G_GSIZE_FORMAT " from the buffer list", buf->fd, buf->size);
    alloc->buffer_list = g_slist_remove_link (alloc->buffer_list, iter);
    _free_buffer (buf, allocator);
    g_slist_free_1 (iter);
  }
}

static void
_insert_buffer_to_list (GstAllocator * allocator, GstMemory * mem, gint buf_fd,
    gsize alloc_size)
{
  GstVesDeliverAllocator *alloc = GST_VESDELIVER_ALLOCATOR (allocator);
  BitstreamBuffer *buf = g_new0 (BitstreamBuffer, 1);

  if (buf) {
    buf->mem = mem;
    buf->fd = buf_fd;
    buf->size = alloc_size;
    buf->used = TRUE;
    g_mutex_lock (&alloc->buf_lock);
    if (g_slist_length (alloc->buffer_list) >= alloc->param.threshold_buf_count) {
      // remove the min unused buffer from the buffer list
      _try_remove_buffer_from_list (allocator);
    }
    if (alloc_size > alloc->max_alloc_buf_size) {
      alloc->max_alloc_buf_size = alloc_size;
      GST_INFO_OBJECT (alloc, "Update max_alloc_buf_size to %lu",
          alloc->max_alloc_buf_size);
    }
    alloc->buffer_list =
        g_slist_insert_sorted (alloc->buffer_list, buf, _buf_size_cmp_func);
    g_mutex_unlock (&alloc->buf_lock);
    GST_INFO_OBJECT (alloc,
        "Add the buffer with fd=%d size=%" G_GSIZE_FORMAT
        " to buffer list, list size=%u", buf->fd, buf->size,
        g_slist_length (alloc->buffer_list));
  } else {
    GST_ERROR_OBJECT (alloc, "Failed to new BitstreamBuffer");
  }
}

static GstMemory *
_alloc_buffer (GstAllocator * allocator, gsize alloc_size)
{
  GstVesDeliverAllocator *alloc = GST_VESDELIVER_ALLOCATOR (allocator);
  gint buf_fd = -1;
  GstMemory *mem = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean is_secure_heap = (alloc->param.secure_mode == SECURE_COPY);

#ifdef USE_DMAHEAP
  const char *heap_name = (alloc->param.secure_mode == LEND_DMABUF
      && alloc->param.buf_contiguous) ? contiguous_mem_heap_name : "qcom,system-uncached";

  GST_DEBUG_OBJECT (alloc, "will allocate dma buf fd for sz %" G_GSIZE_FORMAT " bytes", alloc_size);
  if (is_secure_heap) {
    buf_fd =
        alloc->alloc_fd (alloc->dmaheap_allocator, "system-secure", alloc_size,
        0, 0);
  } else {
    buf_fd =
        alloc->alloc_fd (alloc->dmaheap_allocator, heap_name, alloc_size, 0, 0);
  }

  if (buf_fd < 0) {
    GST_ERROR_OBJECT (alloc, "failed to allocate buffer from DMA %s heap", is_secure_heap ?
        "system-secure" : heap_name);
    ret = GST_FLOW_ERROR;
  } else {
    GST_DEBUG_OBJECT (alloc, "allocate buf fd successfully, dma fd = %d", buf_fd);
  }
#else
  guint heap_mask = (alloc->param.secure_mode == LEND_DMABUF
      && alloc->param.buf_contiguous) ? ION_HEAP (ION_DISPLAY_HEAP_ID) : ION_HEAP (ION_SYSTEM_HEAP_ID);
  guint flags = 0;
  int rc = -EINVAL;

  if (is_secure_heap) {
    flags = ION_FLAG_SECURE | ION_FLAG_CP_BITSTREAM;
    heap_mask =
        ION_HEAP (ION_SECURE_HEAP_ID) | ION_HEAP (ION_SECURE_DISPLAY_HEAP_ID);
  }

  GST_DEBUG_OBJECT (alloc, "will allocate ion buf fd for sz %" G_GSIZE_FORMAT " bytes", alloc_size);
  rc = alloc->ion_alloc_fd (alloc->ion_fd, alloc_size, 0, heap_mask, flags,
      &buf_fd);

  if (rc || buf_fd < 0) {
    GST_ERROR_OBJECT (alloc, "ion_alloc_fd failed with rc = %d", rc);
    ret = GST_FLOW_ERROR;
  } else {
    GST_DEBUG_OBJECT (alloc, "allocate buf fd successfully, ion fd = %d", buf_fd);
  }
#endif

  if (ret == GST_FLOW_OK) {
    mem = gst_dmabuf_allocator_alloc (allocator, buf_fd, alloc_size);//Created gstmemory will close fd when gstmemory finalize. If want to control it, use gst_dmabuf_allocator_alloc_with_flags.
    if (mem) {
      GST_INFO_OBJECT (alloc,
          "Allocate %s gstmemory with size = %" G_GSIZE_FORMAT ", fd = %d",
          is_secure_heap ? "secure" : "normal", alloc_size, buf_fd);

      if (alloc->param.buf_recycle) {
        /* save the buffer to list for recycling */
        _insert_buffer_to_list (allocator, mem, buf_fd, alloc_size);
      }
    } else {
      GST_ERROR_OBJECT (alloc, "Failed to allocate gstmemory for sz %" G_GSIZE_FORMAT ", fd %d, will close fd", alloc_size, buf_fd);
      close(buf_fd);
    }
  }

  return mem;
}

static GstMemory *
gst_vesdeliver_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  GstVesDeliverAllocator *alloc = GST_VESDELIVER_ALLOCATOR (allocator);
  GstMemory *mem = NULL;
  guint list_size = 0;
  gboolean need_alloc = TRUE;
  gsize alloc_size = ALIGN (size, 4096);

  if (alloc->param.buf_recycle) {
    mem = _acquire_buffer (allocator, alloc_size);
    if (!mem) {
      list_size = g_slist_length (alloc->buffer_list);
      if (list_size >= alloc->param.threshold_buf_count + ALLOC_BUFFER_COUNT_INCREMENT) {
        GST_ERROR_OBJECT (alloc, "allocated buffer count reach the limit %d",
            alloc->param.threshold_buf_count + ALLOC_BUFFER_COUNT_INCREMENT);
        need_alloc = FALSE;
      }
    } else {
      need_alloc = FALSE;
    }
  }

  if (need_alloc) {
    mem = _alloc_buffer (allocator, alloc_size);
  }

  return mem;
}

static void
_recycle_buffer (GstAllocator * allocator, GstMemory * mem)
{
  GstVesDeliverAllocator *alloc = GST_VESDELIVER_ALLOCATOR (allocator);
  AllocatorParameter *param = &alloc->param;
  GSList *iter = NULL;

  g_mutex_lock (&alloc->buf_lock);
  for (iter = alloc->buffer_list; iter; iter = iter->next) {
    BitstreamBuffer *buf = (BitstreamBuffer *) iter->data;
    if (buf && mem == buf->mem) {
      GST_DEBUG_OBJECT (alloc,
          "The shared buffer with fd=%d size=%ld mem=%p can be reused",
          buf->fd, buf->size, buf->mem);
      buf->used = FALSE;
#ifdef USE_DMAHEAP
      if (param->secure_mode == LEND_DMABUF && param->vm_instance
          && param->ReclaimDmabuf) {
        int ret = -1;
        ret = param->ReclaimDmabuf (param->vm_instance, buf->fd, 0);
        if (0 == ret) {
          GST_DEBUG_OBJECT (alloc,
              "Reclaim the dmabuf with fd=%d successfully", buf->fd);
        } else {
          GST_ERROR_OBJECT (alloc, "Failed to reclaim the dmabuf with fd=%d ret=%d",
              buf->fd, ret);
        }
      }
#else
#ifdef ION_FLAG_ION_LEND_BUF
      if (param->secure_mode == LEND_DMABUF && alloc->ion_reclaim_buf) {
        int ret = -1;
        ret = alloc->ion_reclaim_buf(alloc->ion_fd, buf->fd, ION_VMID_CP_BITSTREAM);
        if (0 == ret) {
          GST_DEBUG_OBJECT (alloc,
              "Reclaim the ionbuf with fd=%d successfully.", buf->fd);
        } else {
          GST_ERROR_OBJECT (alloc, "Failed to reclaim the ionbuf with fd=%d ret=%d",
              buf->fd, ret);
        }
      }
#endif
#endif
      g_cond_signal (&alloc->buf_cond);
    }
  }
  g_mutex_unlock (&alloc->buf_lock);
}

static void
gst_vesdeliver_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstVesDeliverAllocator *alloc = GST_VESDELIVER_ALLOCATOR (allocator);
  AllocatorParameter *param = &alloc->param;

  if (param->buf_recycle) {
    _recycle_buffer (allocator, mem);
  } else {
    GST_ALLOCATOR_CLASS (parent_class)->free (allocator, mem);
  }
}

static void
gst_vesdeliver_allocator_class_init (GstVesDeliverAllocatorClass * klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);

  obj_class->finalize = GST_DEBUG_FUNCPTR (gst_vesdeliver_allocator_finalize);
  allocator_class->alloc = GST_DEBUG_FUNCPTR (gst_vesdeliver_allocator_alloc);
  allocator_class->free = GST_DEBUG_FUNCPTR (gst_vesdeliver_allocator_free);
}
