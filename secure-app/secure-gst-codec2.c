/*
* Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted (subject to the limitations in the
* disclaimer below) provided that the following conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*
*     * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
* GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
* HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
* GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
* IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <gst/gst.h>
#include <glib.h>
#include <sys/mman.h>
#include "crypto.h"

#define strlcpy g_strlcpy
Crypto* crypto = NULL;

int secure_copy (int dstbuf_fd, void* srcbuf, uint32_t* pdatalen, void* param) {
  SecureCopyResult ret = crypto_copy (crypto, SECURE_COPY_NONSECURE_TO_SECURE, srcbuf, dstbuf_fd, pdatalen);
  if (ret != SECURE_COPY_SUCCESS) {
    g_error ("secure copy failed, ret:%d dstbuf_fd:%d srcbuf:%p datalen:%d param:%p",
             ret, dstbuf_fd, srcbuf, *pdatalen, param);
  }

  return 0;
}

static void
element_setup (GstElement * playbin, GstElement * element, GQueue * elts)
{
  if (strstr(GST_OBJECT_NAME (element), "qcodec2")) {
    g_debug ("found QTI codec2 vdec element");
    g_object_set (element, "secure", 1, NULL);
    g_object_set (element, "data-copy-func", (void*)&secure_copy, NULL);
    g_object_set (element, "data-copy-func-param", (void*)element, NULL);
  }
}

int
main (int argc, char *argv[])
{
  gchar* str_playbin = g_strdup("playbin uri=");
  gchar *concat_str = NULL;
  GstElement *pipeline = NULL;
  GstBus *bus = NULL;
  GstMessage *msg = NULL;

  crypto = (Crypto*)g_new0(Crypto, 1);
  if (!crypto) {
    g_error ("failed to alloc Crypto");
    goto CLEAN;
  }

  crypto_init (crypto);
  concat_str = g_strconcat (str_playbin, argv[1], NULL);
  g_debug ("Pipeline is :%s", concat_str);

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Build the pipeline */
  pipeline = gst_parse_launch (concat_str, NULL);
  if (!pipeline) {
    g_error ("failed to create pipeline");
    goto CLEAN;
  }

  g_signal_connect (pipeline, "element-setup", G_CALLBACK (element_setup), NULL);

  /* Start playing */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Wait until error or EOS */
  bus = gst_element_get_bus (pipeline);
  if (!bus) {
    g_error ("failed to get bus");
    goto CLEAN;
  }
  msg =
      gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  /* See next tutorial for proper error message handling/parsing */
  if (msg && GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    g_error ("An error occurred! Re-run with the GST_DEBUG=*:WARN environment "
        "variable set for more details.");
  }

CLEAN:
  /* Free resources */
  if (crypto) {
    crypto_deinit (crypto);
    g_free (crypto);
  }
  if (msg)
    gst_message_unref (msg);
  if (bus)
    gst_object_unref (bus);
  if (pipeline) {
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
  }

  return 0;
}
