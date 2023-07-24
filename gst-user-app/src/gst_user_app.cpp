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

#include "gst_user_app.h"


int main(int argc, char*argv[]) {

    GstElement *pipeline = nullptr,
               *filesrc = nullptr,
               *filesink = nullptr,
               *rawvideoparse = nullptr,
               *qtic2venc = nullptr,
               *h264parse = nullptr,
               *mp4mux = nullptr,
               *capsfilter = nullptr,
               *fakesink = nullptr;

    GstCaps *caps = nullptr;

    GstBus *bus = nullptr;

    GstMessage *msg = nullptr;

    GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;


    //Inititalize Gstreamer and create Elems
    gst_init(&argc, &argv);

    pipeline = gst_pipeline_new("enc_longrun");

    filesrc = gst_element_factory_make("filesrc", "file-source");

    rawvideoparse = gst_element_factory_make("rawvideoparse", "raw-parse");

    qtic2venc = gst_element_factory_make("qtic2venc", "video-encoder");

    h264parse = gst_element_factory_make("h264parse", "h264-parse");

    mp4mux = gst_element_factory_make("mp4mux", "mp4-mux");

    filesink = gst_element_factory_make("filesink", "file-sink");

    /*
    //Below gst elemts for debug purpose
    capsfilter = gst_element_factory_make("capsfilter", "caps-filter");

    fakesink = gst_element_factory_make("fakesink", "fakesink");
    */

    if (!pipeline || !filesrc || !rawvideoparse || !qtic2venc || !h264parse ||!mp4mux || !filesink) {
        g_printerr("Failed to create one or more elements.. EXT\n");
        return -1;
    }

    //TODO: Read gst elemt properties from cmdline args or create a config file.
    g_object_set(G_OBJECT(filesrc), "location", "/data/temp/beattest/input/input.yuv", NULL);
    g_object_set(G_OBJECT(filesink), "location", "/data/temp/beattest/output/output.mp4", NULL);
    g_object_set(G_OBJECT(filesink), "sync", TRUE, NULL);
    g_object_set(G_OBJECT(rawvideoparse), "width", 640, "height", 480, "framerate", 60, 1, "format", GST_VIDEO_FORMAT_NV12, NULL);

    //Build and Link the pipeline
    gst_bin_add_many(GST_BIN(pipeline), filesrc, rawvideoparse, qtic2venc, h264parse, mp4mux, filesink, NULL);

    if (!gst_element_link_many(filesrc, rawvideoparse,  qtic2venc, h264parse, mp4mux, filesink, NULL)) {
        g_printerr("Elements couldn't be linked. EXT\n");
        gst_object_unref(pipeline);
        return -1;
    }

    //Set the pipeline to playing
    ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to set the ppeline to Playing state. EXT\n");
        gst_object_unref(pipeline);
        return -1;
    }

    //Wait until EOS
    bus = gst_element_get_bus(pipeline);
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                     GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

    if (msg != nullptr) {
        GError *err = nullptr;
        gchar *debug_info = nullptr;

        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR:
                gst_message_parse_error(msg, &err, &debug_info);
                g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
                g_printerr("Debug info: %s\n", debug_info?debug_info:"none");
                g_clear_error(&err);
                g_free(debug_info);
                break;
            case GST_MESSAGE_EOS:
                g_print("EOS received.\n");
                break;
            default:
                //Unhandled cases
                break;
        }
        gst_message_unref(msg);
    }

    //Free Resr
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return 0;
}



