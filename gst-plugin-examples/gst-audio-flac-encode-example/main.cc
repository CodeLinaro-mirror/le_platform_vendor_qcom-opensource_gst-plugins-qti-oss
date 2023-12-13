/*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
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

/*
* Application:
* GStreamer Single Audio Encoder
*
* Description:
* This application Encodes the flac audio and
* output file in /data/audioencoded.flac
*
* Usage and help:
* gst-audio-flac-encode-example --help
*
*/

#include <gst/gst.h>
#include <glib.h>
#include <string.h>


gint
main (int argc, char *argv[])
{
  GstElement *pipeline, *pulsesrc, *main_capsfilter, *audioconvert, *encoder;
  GstElement *filesink;
  GstBus *bus;
  GstMessage *msg;
  GstCaps *filtercaps;
  GError *error = NULL;
  GOptionContext *context;

  GOptionEntry entries[] = {
    { "FlacEncoding", 'e', 0, G_OPTION_ARG_NONE,NULL, "FlacEncoding", NULL },
    { NULL }
  };

  if ((context = g_option_context_new ("DESCRIPTION")) != NULL) {
    g_option_context_add_main_entries (context, entries, NULL);
    g_option_context_add_group (context, gst_init_get_option_group ());

    if (!g_option_context_parse (context, &argc, &argv, &error)) {
      g_printerr ("option parsing failed: %s\n", error->message);
      return -1;
    }
  } else {
    g_printerr ("ERROR: Failed to create options context!\n");
    return -EFAULT;
  }

  g_option_context_free (context);
  g_error_free (error);

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  g_print ("\n\n Audio Encoding i.e flac file ....\n");

  pipeline = gst_pipeline_new ("pipeline");

  /* create a source */
  pulsesrc = gst_element_factory_make ("pulsesrc", "pulsesrc");

  /* create a capsfilter */
  main_capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");

  /* create audioconvert */
  audioconvert = gst_element_factory_make ("audioconvert", "audioconvert");

  /* create audio encoder */
  encoder = gst_element_factory_make ("flacenc", "encoder");

  /* create filesink */
  filesink = gst_element_factory_make ("filesink", "filesink");

  /* Check if all elements are created successfully */
  if (!pulsesrc || !main_capsfilter || !audioconvert ||
      !encoder || !filesink) {
    g_printerr ("Not all elements could be created.\n");
    return FALSE;
  }

  g_object_set (G_OBJECT (filesink), "location", "/data/audioencoded.flac",
                   NULL);

  /* Configure the stream caps */
  filtercaps = gst_caps_new_simple ("audio/x-raw",
                                    "format", G_TYPE_STRING, "S16LE",
                                    "rate", G_TYPE_INT, 48000,
                                    "channels", G_TYPE_INT, 1,
                                     NULL);
  g_object_set (G_OBJECT (main_capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  /* Add elements to the pipeline and link them */
  g_print ("Adding all elements to the pipeline...\n");
  gst_bin_add_many (GST_BIN (pipeline), pulsesrc, main_capsfilter, audioconvert,
                    encoder, filesink, NULL);

  /* Linking the stream */
  gst_element_link_many (pulsesrc, main_capsfilter, audioconvert, encoder,
                         filesink, NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Wait until error or EOS */
  bus = gst_element_get_bus (pipeline);
  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
                                    GST_MESSAGE_EOS);

  /* Free resources */
  if (msg != NULL)
    gst_message_unref (msg);

  gst_object_unref (bus);

  /* Setting pipeline to NULL state */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  gst_deinit ();

  return 0;
}
