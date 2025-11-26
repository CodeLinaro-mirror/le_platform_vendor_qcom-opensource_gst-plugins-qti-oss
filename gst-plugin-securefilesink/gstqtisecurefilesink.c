/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include "gstqtisecurefilesink.h"

GST_DEBUG_CATEGORY_STATIC (gst_qtisecurefilesink_debug_category);
#define GST_CAT_DEFAULT gst_qtisecurefilesink_debug_category
#define LIBCONTENTCOPY_PATH "/usr/lib/libcontentcopy.so"
#define DEFAULT_PATH "/data/dump.yuv"
#define MAX_BUF_SIZE 6000000

/* prototypes */


static void gst_qtisecurefilesink_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_qtisecurefilesink_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_qtisecurefilesink_finalize (GObject * object);

static gboolean gst_qtisecurefilesink_start (GstBaseSink * sink);
static gboolean gst_qtisecurefilesink_stop (GstBaseSink * sink);
static gboolean gst_qtisecurefilesink_unlock (GstBaseSink * sink);
static GstFlowReturn gst_qtisecurefilesink_render (GstBaseSink * sink,
    GstBuffer * buffer);

enum
{
  PROP_0
};

/* pad templates */

static GstStaticPadTemplate gst_qtisecurefilesink_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
    );


/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstQtisecurefilesink, gst_qtisecurefilesink,
    GST_TYPE_BASE_SINK,
    GST_DEBUG_CATEGORY_INIT (gst_qtisecurefilesink_debug_category,
        "qtisecurefilesink", 0,
        "debug category for qtisecurefilesink element"));

static void
gst_qtisecurefilesink_class_init (GstQtisecurefilesinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS (klass),
      &gst_qtisecurefilesink_sink_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Secure fd to filesink", "Generic", "Secure fd to filesink",
      "qualcomm.com");

  gobject_class->set_property = gst_qtisecurefilesink_set_property;
  gobject_class->get_property = gst_qtisecurefilesink_get_property;
  gobject_class->finalize = gst_qtisecurefilesink_finalize;
  base_sink_class->start = GST_DEBUG_FUNCPTR (gst_qtisecurefilesink_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR (gst_qtisecurefilesink_stop);
  base_sink_class->unlock = GST_DEBUG_FUNCPTR (gst_qtisecurefilesink_unlock);
  base_sink_class->render = GST_DEBUG_FUNCPTR (gst_qtisecurefilesink_render);

}

static void
gst_qtisecurefilesink_init (GstQtisecurefilesink * qtisecurefilesink)
{
  qtisecurefilesink->lib_handle= NULL;
  qtisecurefilesink->l_QSEEComHandle = NULL;
  qtisecurefilesink->buf_size = MAX_BUF_SIZE;

}

void
gst_qtisecurefilesink_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQtisecurefilesink *qtisecurefilesink = GST_QTISECUREFILESINK (object);

  GST_DEBUG_OBJECT (qtisecurefilesink, "set_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_qtisecurefilesink_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstQtisecurefilesink *qtisecurefilesink = GST_QTISECUREFILESINK (object);

  GST_DEBUG_OBJECT (qtisecurefilesink, "get_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_qtisecurefilesink_finalize (GObject * object)
{
  GstQtisecurefilesink *qtisecurefilesink = GST_QTISECUREFILESINK (object);

  GST_DEBUG_OBJECT (qtisecurefilesink, "finalize");

  /* clean up object here */

  G_OBJECT_CLASS (gst_qtisecurefilesink_parent_class)->finalize (object);
}

/* start and stop processing, ideal for opening/closing the resource */
static gboolean
gst_qtisecurefilesink_start (GstBaseSink * sink)
{
  GstQtisecurefilesink *qtisecurefilesink = GST_QTISECUREFILESINK (sink);
  gulong res = 0;
  gchar *err = NULL;

  GST_DEBUG_OBJECT (qtisecurefilesink, "start");

  qtisecurefilesink->lib_handle = dlopen(LIBCONTENTCOPY_PATH, RTLD_NOW);
  if (qtisecurefilesink->lib_handle == NULL) {
    err = dlerror();
    if (err != NULL) {
      GST_ERROR ("Cannot load library dlerror():%s", err);
    }
    return FALSE;
  }


  *(void **)(&qtisecurefilesink->cpc.Content_Protection_Set_AppName)= dlsym(qtisecurefilesink->lib_handle, "Content_Protection_Set_AppName");
  if(qtisecurefilesink->cpc.Content_Protection_Set_AppName == NULL) {
    GST_ERROR ("dlsym Content_Protection_Set_AppName failed!");
    return FALSE;
  }

  *(void **)(&qtisecurefilesink->cpc.Content_Protection_Copy_Init)= dlsym(qtisecurefilesink->lib_handle, "Content_Protection_Copy_Init");
  if(qtisecurefilesink->cpc.Content_Protection_Copy_Init == NULL) {
    GST_ERROR ("dlsym Content_Protection_Copy_Init failed!");
    return FALSE;
  }

  *(void **)(&qtisecurefilesink->cpc.Content_Protection_Copy)= dlsym(qtisecurefilesink->lib_handle, "Content_Protection_Copy");
  if(qtisecurefilesink->cpc.Content_Protection_Copy == NULL) {
    GST_ERROR ("dlsym Content_Protection_Copy failed!");
    return FALSE;
  }

  *(void **)(&qtisecurefilesink->cpc.Content_Protection_Copy_Terminate)= dlsym(qtisecurefilesink->lib_handle, "Content_Protection_Copy_Terminate");
  if(qtisecurefilesink->cpc.Content_Protection_Copy_Terminate == NULL) {
    GST_ERROR ("dlsym Content_Protection_Copy_Terminate failed!");
    return FALSE;
  }

  res = qtisecurefilesink->cpc.Content_Protection_Set_AppName("smpcpyap64");
  if (res != SAMPLE_CLIENT_SUCCESS) {
    GST_ERROR ("Content_Protection_Set_AppName failed! err:0x%08lx",res);
    return FALSE;
  }

  res = qtisecurefilesink->cpc.Content_Protection_Copy_Init(&qtisecurefilesink->l_QSEEComHandle);
  if (res != SAMPLE_CLIENT_SUCCESS) {
    GST_ERROR ("Content_Protection_Copy_Init failed! err:0x%08lx",res);
    if (qtisecurefilesink->l_QSEEComHandle == NULL)
      GST_ERROR ("l_QSEEComHandle is NULL !!");
    return FALSE;
  }
  else {
     GST_DEBUG_OBJECT (qtisecurefilesink,"cpc.Content_Protection_Copy_Init success");
  }

  qtisecurefilesink->buf = (guint8*)malloc(qtisecurefilesink->buf_size);
  
  qtisecurefilesink->fp = fopen(DEFAULT_PATH, "a");

  return TRUE;
}

static gboolean
gst_qtisecurefilesink_stop (GstBaseSink * sink)
{
  GstQtisecurefilesink *qtisecurefilesink = GST_QTISECUREFILESINK (sink);

  GST_DEBUG_OBJECT (qtisecurefilesink, "stop");
   if (qtisecurefilesink->lib_handle)
    dlclose(qtisecurefilesink->lib_handle);

  g_free(qtisecurefilesink->buf);
  fflush(qtisecurefilesink->fp);
  fclose(qtisecurefilesink->fp);

  return TRUE;
}

/* unlock any pending access to the resource. subclasses should unlock
 * any function ASAP. */
static gboolean
gst_qtisecurefilesink_unlock (GstBaseSink * sink)
{
  GstQtisecurefilesink *qtisecurefilesink = GST_QTISECUREFILESINK (sink);

  GST_DEBUG_OBJECT (qtisecurefilesink, "unlock");

  return TRUE;
}

static GstFlowReturn
gst_qtisecurefilesink_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstQtisecurefilesink *qtisecurefilesink = GST_QTISECUREFILESINK (sink);
  guint in_fd;
  guint ret = 0;
  gint size = 0;
  guint copy_size;

  GST_DEBUG_OBJECT (qtisecurefilesink, "render");

  if (gst_is_fd_memory (gst_buffer_peek_memory (buffer, 0))) {
    in_fd = gst_fd_memory_get_fd (gst_buffer_peek_memory (buffer, 0));

    size = gst_buffer_get_size(buffer); 

    ret = qtisecurefilesink->cpc.Content_Protection_Copy  (qtisecurefilesink->l_QSEEComHandle,
                                  qtisecurefilesink->buf,
                                  size,
                                  (uint32_t)in_fd,
                                  0,
                                  (uint32_t*)&copy_size,
                                  (SampleClientCopyDir)SAMPLECLIENT_COPY_SECURE_TO_NONSECURE
                                  );

    if (ret != SAMPLE_CLIENT_SUCCESS) {
      GST_ERROR ("Secure to Non Secure buffer copy failed! ");
      return GST_FLOW_ERROR;
    }
    else {
      GST_ERROR ("Secure to Non Secure buffer copy SUCCESS size %d", size);
      fwrite(qtisecurefilesink->buf, sizeof(guint8), size, qtisecurefilesink->fp);
    }
    
  }

  return GST_FLOW_OK;
}

static gboolean
plugin_init (GstPlugin * plugin)
{

  return gst_element_register (plugin, "qtisecurefilesink", GST_RANK_NONE,
      GST_TYPE_QTISECUREFILESINK);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtisecurefilesink,
    "QTI non secure to secure copy plugin",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)

