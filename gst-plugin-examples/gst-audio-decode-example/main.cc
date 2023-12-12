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
* GStreamer Single Audio Decoder
*
* Description:
* This application Decodes the audio i.e. mp3 and raw
*
* help:
* gst-audio-decoding-example --help
*
* Usage:
* For wavefile: gst-audio-decoding-example path/filename --wavfile=1
*
* For mp3: gst-audio-decoding-example path/filename --wavfile=0
*/

#include <gst/gst.h>
#include <stdio.h>

gint
main (gint argc, gchar *argv[])
{
  GstElement *pipeline, *filesrc, *parse, *decoder, *audiosink;
  GstBus *bus;
  GstMessage *msg;
  bool wav = TRUE;
  GError *error = NULL;
  GOptionContext *context;

  GOptionEntry entries[] = {
    { "format", 'w', 0, G_OPTION_ARG_INT, &wav, "1:wav and 0:mp3","source" },
    { NULL }
  };

  if ((context = g_option_context_new ("For Audio Decoding")) != NULL) {
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

  g_print ("\n Audio Decoding ...\n");

  pipeline = gst_pipeline_new ("pipeline");

  /* create a source */
  filesrc = gst_element_factory_make ("filesrc", "source");

  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);

  /* and an audio parser */
  parse = gst_element_factory_make ("mpegaudioparse", "parse");
  if (wav)
  {
     parse = gst_element_factory_make ("wavparse", "parse");
  }

  /* and an audio decoder */
  decoder = gst_element_factory_make ("mpg123audiodec", "decoder");

  /* and an audio sink */
  audiosink = gst_element_factory_make ("pulsesink", "play_audio");

  /* Check if all elements are created successfully*/
  if (!pipeline || !parse || !decoder || !audiosink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

 /* add objects to the main pipeline and link src to sink */
  if (wav) {
    gst_bin_add_many (GST_BIN (pipeline), filesrc, parse, audiosink, NULL);
    gst_element_link_many (filesrc, parse, audiosink, NULL);
  } else {
    gst_bin_add_many (GST_BIN (pipeline), filesrc, parse, decoder, audiosink,
        NULL);
    gst_element_link_many (filesrc, parse, decoder, audiosink, NULL);
  }

  /* Start playing */
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
