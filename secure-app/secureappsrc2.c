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

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <linux/msm_ion.h>
#include <ion/ion.h>
#include <sys/mman.h>
#include <linux/dma-buf.h>

#define SECURE_PLAYBACK
#include "crypto.h"

/************************************************************************/
/*              #DEFINES                            */
/************************************************************************/
#define H264_START_CODE 0x00000001
#define NALU_TYPE_SPS       7
#define NALU_TYPE_PPS       8
#define NALU_TYPE_SEI       6
#define NALU_TYPE_IDR       5
#define NALU_TYPE_NON_IDR   1
#define NALU_TYPE_AUD       9

#define H265_START_CODE 0x00000001
#define HEVC_NALU_TYPE_MASK 0x7F
#define HEVC_NAL_UNIT_TYPE_TRAIL_N 0x00
#define HEVC_NALU_TYPE_NON_IDR 1
#define HEVC_NALU_TYPE_IDR 19
#define HEVC_NAL_UNIT_TYPE_IDR_N_LP 20
#define HEVC_NAL_UNIT_TYPE_RSV_VCL_N10 10
#define HEVC_NAL_UNIT_TYPE_RSV_VCL_N12 12
#define HEVC_NAL_UNIT_TYPE_RSV_VCL_N14 14
#define HEVC_NAL_UNIT_TYPE_RSV_VCL_R11 11
#define HEVC_NAL_UNIT_TYPE_RSV_VCL_R13 13
#define HEVC_NAL_UNIT_TYPE_RSV_VCL_R15 15
#define HEVC_NAL_UNIT_TYPE_RSV_IRAP_VCL22 22
#define HEVC_NAL_UNIT_TYPE_RSV_IRAP_VCL23 23
#define HEVC_NAL_UNIT_TYPE_VCL_LIMIT 23
#define HEVC_NAL_UNIT_TYPE_RESERVED_START 0x18
#define HEVC_NAL_UNIT_TYPE_RESERVED_END 0x1F
#define HEVC_NALU_TYPE_VPS 32
#define HEVC_NALU_TYPE_SPS 33
#define HEVC_NALU_TYPE_PPS 34
#define HEVC_NALU_TYPE_AUD 35
#define HEVC_NALU_TYPE_SEI 39
#define HEVC_NAL_UNIT_TYPE_SUFFIX_SEI 0x28
#define HEVC_NAL_UNIT_TYPE_RESERVED_UNSPECIFIED 0x29
#define HEVC_FIRST_MB_IN_SLICE_MASK 0x80

#define MAX_WIDTH               4096
#define MAX_HEIGHT              2304
#define MAX_INPUT_BUFFER_SIZE   (((MAX_WIDTH) * (MAX_HEIGHT) * (3) / (2)) / (2))

typedef struct _secureappsrc {
  GMainLoop *loop;
  GQueue *sec_buf_queue;
  Crypto *crypto;
  GMutex file_lock;
  GMutex buf_lock;
  GCond buf_cond;
  void *sec_buf_addr;
} secureappsrc;

FILE *fp = NULL;
static uint8_t *input_nonsecure_buffer = NULL;
int64_t timeStamp_fromIVF = -1;
uint32_t ts_scaler_fromIVF_d = 0;
uint32_t ts_scaler_fromIVF_n = 0;
float fps = 30.0;

static int (*Read_Buffer)(uint8_t *data);

/**************************************************************************/
/*              STATIC DECLARATIONS                       */
/**************************************************************************/
static int Read_Buffer_From_H264_Start_Code_File(uint8_t *data);
static int Read_Buffer_From_H265_Start_Code_File(uint8_t *data);
static int Read_Buffer_From_Size_Nal(uint8_t *data);
static int Read_Buffer_From_Ivf_File(uint8_t *data);

static void onNewPad(GstElement *decodebin, GstPad *pad, GstElement *userData)
{
  GstPad *newpad;
  newpad = gst_element_get_static_pad(GST_ELEMENT(userData), "sink");
  gst_pad_link(pad, newpad);
  g_object_unref(newpad);
}

static void onOMXVideoDecCreated (GstBin * bin, GstBin * sub_bin,
    GstElement * element, gpointer user_data)
{
#ifdef SECURE_PLAYBACK
  // for the secure playback, make sure the following properties have been set
  g_object_set (element, "secure", 1, NULL);
#endif
  g_object_set (element, "input-buffer-sharing", 1, NULL);
}

/* The function is triggered when element GST_APPSRC queue is empty. It requires more buffers to
   fill. Firstly, it reads raw video data, then copies the data to input buffer of OMX input
   port.
*/
static void onNeedData(GstElement *appsrc, guint dataSize, secureappsrc *secureappsrc)
{
  GstBuffer *buffer;
  int length = 0;
  int len = 0;
  OMX_BUFFERHEADERTYPE *free_sec_ion_buf = NULL;

  g_mutex_lock (&secureappsrc->file_lock);
RETRY:
  g_mutex_lock (&secureappsrc->buf_lock);
  free_sec_ion_buf = (OMX_BUFFERHEADERTYPE *)g_queue_pop_head (secureappsrc->sec_buf_queue);
  if (free_sec_ion_buf == NULL) {
    GST_DEBUG ("no empty input secure buffer");
    g_cond_wait (&secureappsrc->buf_cond, &secureappsrc->buf_lock);
    g_mutex_unlock (&secureappsrc->buf_lock);
    GST_DEBUG ("cond waited");
    goto RETRY;
  } else {

  }

  g_mutex_unlock (&secureappsrc->buf_lock);
  GST_DEBUG ("sec2 etb:%p size:%d sec ion nAllocLen:%d fd:%d len:%d\n",
    free_sec_ion_buf, free_sec_ion_buf->nFilledLen, free_sec_ion_buf->nAllocLen, free_sec_ion_buf->pBuffer, length);
  length = Read_Buffer(input_nonsecure_buffer);
  free_sec_ion_buf->nFilledLen = length;
  g_mutex_unlock (&secureappsrc->file_lock);
  if (length > 0) {
#ifdef SECURE_PLAYBACK
    SecureCopyResult ret1 = crypto_copy (secureappsrc->crypto, SECURE_COPY_NONSECURE_TO_SECURE,
      input_nonsecure_buffer, (unsigned long)free_sec_ion_buf->pBuffer, &length);
    if (ret1 != SECURE_COPY_SUCCESS) {
      GST_ERROR ("copy non-secure buf to secure buf failed");
    }
#else
    char *bufaddr = (char*)mmap(NULL, free_sec_ion_buf->nAllocLen, PROT_READ|PROT_WRITE, MAP_SHARED,
      (gint64)free_sec_ion_buf->pBuffer, 0);
    if (bufaddr == MAP_FAILED) {
      GST_ERROR ("mmap failed");
    } else {
      memcpy (bufaddr, input_nonsecure_buffer, length);
    }
#endif
  }


  if (length > 0)
  {
    GstBuffer *buffer;
    GST_DEBUG ("sec1 etb:%p size:%d sec ion nAllocLen:%d fd:%d len:%d\n",free_sec_ion_buf,
      free_sec_ion_buf->nFilledLen, free_sec_ion_buf->nAllocLen, free_sec_ion_buf->pBuffer, length);
    buffer = gst_buffer_new_allocate(NULL, sizeof(OMX_BUFFERHEADERTYPE*), NULL);
    gst_buffer_fill (buffer, 0, &free_sec_ion_buf, sizeof(OMX_BUFFERHEADERTYPE*));
    gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);
  }
  else
  {
    gst_app_src_end_of_stream(GST_APP_SRC(appsrc));
    GST_DEBUG ("sent eos");
  }
}

static gboolean msg_handler(GstBus *bus, GstMessage *msg, gpointer data)
{
  secureappsrc *appsrc = (secureappsrc *)data;
  GMainLoop *loop = appsrc->loop;
  OMX_BUFFERHEADERTYPE *omx_buf_header = NULL;

  if (msg->type == GST_MESSAGE_EOS) {
    g_main_loop_quit (loop);
    GST_DEBUG ("the pipeline will be ended because of the EOS message");
  } else if (msg->type == GST_MESSAGE_ERROR) {
    g_main_loop_quit (loop);
    GST_ERROR ("the pipiple post a error");
  } else {
    if (gst_message_has_name (msg, "omx-dec-buf-fd")) {
      const GstStructure *s = gst_message_get_structure (msg);
      gst_structure_get (s, "buf-fd", G_TYPE_POINTER, &omx_buf_header, NULL);
      GST_DEBUG ("buf:%p fd:%d freed\n", omx_buf_header, omx_buf_header->pBuffer);
      g_mutex_lock (&appsrc->buf_lock);
      if (omx_buf_header) {
        g_queue_push_tail (appsrc->sec_buf_queue, omx_buf_header);
        GST_DEBUG ("add buf to queue");
      } else {
        GST_ERROR ("error buf fd:%p", omx_buf_header);
        g_main_loop_quit(loop);
      }
      g_cond_broadcast (&appsrc->buf_cond);
      g_mutex_unlock (&appsrc->buf_lock);
    } else {
      GST_DEBUG ("didn't handle msg type:%d", msg->type);
    }
  }

  return TRUE;
}

static int Read_Buffer_From_H264_Start_Code_File(uint8_t *data)
{
  int byte_read = 0;
  int cnt = 0;
  char abyte=0;
  gboolean hasFrameContent = FALSE;
  gboolean done = FALSE;
  unsigned int code = 0;
  int naluType = 0;
  int newFrame = 0;
  int startcode=0;
  int adjust = 0;
  char *dataptr = (char *)data;

  GST_DEBUG("Inside %s", __FUNCTION__);
  do
  {
    newFrame = 0;
    byte_read = fread(&abyte, 1, 1, fp);
    GST_DEBUG("00 READ Byte[%d] = 0x%x", cnt, dataptr[cnt]);
    if (!byte_read)
    {
      GST_DEBUG("Bytes read Zero, cnt=%d", cnt);
      done = TRUE;
    }

    if (!done)
    {
      code <<= 8;
      code |= (0x000000FF & abyte);
      if (code == H264_START_CODE)
      {
        startcode = 4;
      }
      else if ((code & 0x00ffffff) == H264_START_CODE)
      {
        startcode = 3;
      }
      else
      {
        startcode = 0;
      }

      if (cnt != 0)
      {
        dataptr[cnt++]=abyte;
      }

      if (startcode != 0)
      {
        GST_DEBUG("Read_Buffer_From_H264_File.Found H264_START_CODE");
        byte_read = fread(&abyte, 1, 1, fp);
        if (byte_read == 0)
        {
          GST_DEBUG("Bytes read Zero, cnt=%d", cnt);
          done = TRUE;
        }

        if (!done)
        {
          GST_DEBUG("Read_Buffer_From_H264_File.READ.Byte[%d] = 0x%x", cnt, abyte);
          naluType = abyte & 0x1F;
          if (cnt == 0)
          {
            // dataptr does not have any
            // need to determine where to start capture
            switch(naluType)
            {
              case NALU_TYPE_IDR:
              case NALU_TYPE_NON_IDR:
                // set frame_content and go through to start store
                hasFrameContent = TRUE;
              case NALU_TYPE_SPS:
              case NALU_TYPE_PPS:
                // start collecting
                dataptr[cnt++] = 0;
                dataptr[cnt++] = 0;
                if (startcode == 3)
                {
                  dataptr[cnt++] = 1;
                }
                else
                {
                  dataptr[cnt++] = 0;
                  dataptr[cnt++] = 1;
                }
                dataptr[cnt++]=abyte;
                adjust = startcode + 1;
                break;
              default:
                // do not update cnt: not start of frame yet
                break;
            } // switch
          }
          else
          {
            /* dataptr has something; already start collecting
            need to determine a frame start and end */
            dataptr[cnt++]=abyte; // store the nal type
            switch(naluType)
            {
              case NALU_TYPE_IDR:
              case NALU_TYPE_NON_IDR:
                if (!hasFrameContent)
                {
                  hasFrameContent = TRUE;
                }
                else
                {
                  /* dataptr has frame content
                  need to determine the frame boundary at IDR/NIDR NAL */
                  byte_read = fread(&abyte, 1, 1, fp);
                  if (byte_read == 0)
                  {
                    done = TRUE;
                  }

                  if (!done)
                  {
                    dataptr[cnt++]=abyte; // store it
                    newFrame = abyte & 0x80;
                    if (newFrame)
                    {
                      /* first mb address in the slice is 0 => assume non-ASO,
                      frame-based clip and non-error stream */
                      GST_DEBUG("newFrame startcode:%d", startcode);
                      adjust = -(startcode + 2);
                      fseek(fp, adjust, SEEK_CUR);
                      cnt += adjust;
                      done = TRUE;
                    }
                  }
                }
                break;
              case NALU_TYPE_SPS:
              case NALU_TYPE_PPS:
              case NALU_TYPE_SEI:
              case NALU_TYPE_AUD:
                if (hasFrameContent)
                {
                  // dataptr has frame content
                  // it is the frame boundary if SPS, PPS, SEI or AUD
                  GST_DEBUG("hasFrameContent startcode:%d", startcode);
                  adjust = -(startcode + 1);
                  fseek(fp, adjust, SEEK_CUR);
                  cnt += adjust;
                  done = TRUE;
                }
            } // switch
          } // if cnt == 0
        } // !done
      } // (startcode != 0)
    } // (!done)
  } while ((!done) && (cnt < MAX_INPUT_BUFFER_SIZE));

  return cnt;
}

static int Read_Buffer_From_H265_Start_Code_File(uint8_t *data)
{
  int byte_read = 0;
  int cnt = 0;
  char abyte=0;
  gboolean hasFrameContent = FALSE;
  gboolean done = FALSE;
  unsigned int code = 0;
  int naluType = 0;
  int newFrame = 0;
  int startcode=0;
  int adjust = 0;
  char *dataptr = (char *)data;

  GST_DEBUG("Inside %s", __FUNCTION__);
  do
  {
    newFrame = 0;
    byte_read = fread(&abyte, 1, 1, fp);
    GST_DEBUG("00 READ Byte[%d] = 0x%x", cnt, dataptr[cnt]);
    if (!byte_read)
    {
      GST_DEBUG("Bytes read Zero, cnt=%d", cnt);
      done = TRUE;
    }

    if (!done)
    {
      code <<= 8;
      code |= (0x000000FF & abyte);
      if (code == H265_START_CODE)
      {
        startcode = 4;
      }
      else if ((code & 0x00ffffff) == H265_START_CODE)
      {
        startcode = 3;
      }
      else
      {
        startcode = 0;
      }

      if (cnt != 0)
      {
        dataptr[cnt++]=abyte;
      }

      if (startcode != 0)
      {
        GST_DEBUG("Read_Buffer_From_H265_File.Found H265_START_CODE");
        byte_read = fread(&abyte, 1, 1, fp);
        if (byte_read == 0)
        {
          GST_DEBUG("Bytes read Zero, cnt=%d", cnt);
          done = TRUE;
        }

        if (!done)
        {
          GST_DEBUG("Read_Buffer_From_H265_File.READ.Byte[%d] = 0x%x", cnt, abyte);
          naluType = (abyte & 0x7F) >> 1;
          if (cnt == 0)
          {
            // dataptr does not have any
            // need to determine where to start capture
            switch(naluType)
            {
              case HEVC_NALU_TYPE_IDR:
              case HEVC_NALU_TYPE_NON_IDR:
                // set frame_content and go through to start store
                hasFrameContent = TRUE;
              case HEVC_NALU_TYPE_SPS:
              case HEVC_NALU_TYPE_PPS:
              case HEVC_NALU_TYPE_VPS:
                // start collecting
                dataptr[cnt++] = 0;
                dataptr[cnt++] = 0;
                if (startcode == 3)
                {
                  dataptr[cnt++] = 1;
                }
                else
                {
                  dataptr[cnt++] = 0;
                  dataptr[cnt++] = 1;
                }
                dataptr[cnt++]=abyte;
                //2 Bytes header for H265, so need to read next byte
                byte_read = fread(&abyte, 1, 1, fp);
                if (byte_read == 0)
                {
                  done = TRUE;
                }

                dataptr[cnt++]=abyte;
                adjust = startcode + 2;
                break;
              default:
                // do not update cnt: not start of frame yet
                break;
            } // switch
          }
          else
          {
            /* dataptr has something; already start collecting
            need to determine a frame start and end */
            dataptr[cnt++]=abyte; // store the nal type
            switch(naluType)
            {
              case HEVC_NALU_TYPE_IDR:
              case HEVC_NALU_TYPE_NON_IDR:
                if (!hasFrameContent)
                {
                  hasFrameContent = TRUE;
                }
                else
                {
                  //2 Bytes header for H265, so need to read next byte
                  byte_read = fread(&abyte, 1, 1, fp);
                  if (byte_read == 0)
                  {
                    done = TRUE;
                  }

                  dataptr[cnt++]=abyte;

                  /* dataptr has frame content
                  need to determine the frame boundary at IDR/NIDR NAL */
                  byte_read = fread(&abyte, 1, 1, fp);
                  if (byte_read == 0)
                  {
                    done = TRUE;
                  }

                  if (!done)
                  {
                    dataptr[cnt++]=abyte; // store it
                    newFrame = abyte & 0x80;
                    if (newFrame)
                    {
                      /* first mb address in the slice is 0 => assume non-ASO,
                      frame-based clip and non-error stream */
                      GST_DEBUG("newFrame startcode:%d", startcode);
                      adjust = -(startcode + 3);
                      fseek(fp, adjust, SEEK_CUR);
                      cnt += adjust;
                      done = TRUE;
                    }
                  }
                }
                break;
              case HEVC_NALU_TYPE_SPS:
              case HEVC_NALU_TYPE_PPS:
              case HEVC_NALU_TYPE_VPS:
              case HEVC_NALU_TYPE_SEI:
              case HEVC_NALU_TYPE_AUD:
                if (hasFrameContent)
                {
                  // dataptr has frame content
                  // it is the frame boundary if SPS, PPS, SEI or AUD
                  GST_DEBUG("hasFrameContent startcode:%d", startcode);
                  adjust = -(startcode + 1);
                  fseek(fp, adjust, SEEK_CUR);
                  cnt += adjust;
                  done = TRUE;
                }
            } // switch
          } // if cnt == 0
        } // !done
      } // (startcode != 0)
    } // (!done)
  } while ((!done) && (cnt < MAX_INPUT_BUFFER_SIZE));

  return cnt;
}

static int Read_Frame_Length_From_Ivf_File(int bytes_count, int *frame_length)
{
  int length = 0;
  int len = 0;
  int bytes_read = 0;
  int ret = 0;

  for (int i = 0; i < bytes_count; i++) {
    len = 0;
    ret = fread(&len, 1, 1, fp);
    if (ret > 0) {
      length += ( len<< (8*i));
      bytes_read++;
    }
    else {
      break;
    }
  }

  *frame_length = length;
  return bytes_read;
}

static int Read_Frame_Timestamp_From_Ivf_File(int bytes_count, uint64_t *frame_timestamp)
{
  uint64_t timestamp = 0;
  uint64_t len = 0;
  int bytes_read = 0;
  int ret = 0;

  for (int i = 0; i < bytes_count; i++) {
    len = 0;
    ret = fread(&len, 1, 1, fp);
    if (ret > 0) {
      timestamp += ( len<< (8*i));
      bytes_read++;
    }
    else {
      break;
    }
  }

  *frame_timestamp = timestamp;
  return bytes_read;
}

static int Read_Buffer_From_Ivf_File(uint8_t *data)
{
  int length = 0;
  long frame_ts = 0;
  int bytes_read = 0;

  GST_DEBUG("Inside");
  bytes_read = Read_Frame_Length_From_Ivf_File(4, &length);
  if ( bytes_read > 0 && bytes_read < 4) {
    GST_ERROR("Reading IVF frame size, %d bytes read, not equal to 4 bytes, treat as EOF", bytes_read);
    return 0;
  }else if ( 0 == bytes_read ) {
    printf("0 bytes read from IVF file, really meet EOF\n");
    return 0;
  }
  bytes_read = Read_Frame_Timestamp_From_Ivf_File(8, &frame_ts);
  if ( 8 != bytes_read ) {
    GST_ERROR("Reading IVF frame ts, %d bytes read, not equal to 8 bytes, treat as EOF", bytes_read);
    return 0;
  }
  bytes_read = fread(data, 1, length, fp);
  if (bytes_read != length) {
    GST_ERROR("Reading IVF frame data, %d bytes read, not equal to %d bytes, treat as EOF", bytes_read, length);
    return 0;
  }

  if (ts_scaler_fromIVF_d != 0 && ts_scaler_fromIVF_n != 0) {
    timeStamp_fromIVF = frame_ts * 1000000 * ts_scaler_fromIVF_n / ts_scaler_fromIVF_d;
  }

  return bytes_read;
}

static int Parse_Ivf_File()
{
  unsigned char ivfheader[32] = {0};
  int width;
  int height;
  unsigned int length = 0;
  unsigned long ts1, ts2 = 0;
  int bytes_read = 0;

  int ivfheaderlen = fread(ivfheader, 1, 32, fp);
  if(ivfheaderlen != 32 || !(ivfheader[0] == 'D' && ivfheader[1] == 'K' && ivfheader[2] == 'I' && ivfheader[3] == 'F')) {
    GST_ERROR("IVF file not begin with \"DKIF\", it's corrupted IVF file");
    return -1;
  }
  width = ivfheader[12] + ((unsigned int)ivfheader[13] << 8);
  height = ivfheader[14] + ((unsigned int)ivfheader[15] << 8);
  printf("Parsed from IVF file header, W x H is %d x %d\n", width, height);
  ts_scaler_fromIVF_d = ivfheader[16] + ((unsigned int)ivfheader[17]<<8) + ((unsigned int)ivfheader[18]<<16) + ((unsigned int)ivfheader[19]<<24);
  ts_scaler_fromIVF_n = ivfheader[20] + ((unsigned int)ivfheader[21]<<8) + ((unsigned int)ivfheader[22]<<16) + ((unsigned int)ivfheader[23]<<24);
  printf("Parsed from IVF file header, time base denominator %d, time base numerator %d\n", ts_scaler_fromIVF_d, ts_scaler_fromIVF_n);

  bytes_read = Read_Frame_Length_From_Ivf_File(4, &length);
  if ( bytes_read > 0 && bytes_read < 4) {
    GST_ERROR("Reading IVF frame size, %d bytes read, not equal to 4 bytes, treat as EOF", bytes_read);
    return -1;
  } else if ( 0 == bytes_read ) {
    printf("0 bytes read from IVF file, really meet EOF\n");
    return -1;
  }
  bytes_read = Read_Frame_Timestamp_From_Ivf_File(8, &ts1);
  if ( 8 != bytes_read ) {
    GST_ERROR("Reading IVF frame ts, %d bytes read, not equal to 8 bytes, treat as EOF", bytes_read);
    return -1;
  }
  fseek(fp, length + 4, SEEK_CUR);
  bytes_read = Read_Frame_Timestamp_From_Ivf_File(8, &ts2);
  if ( 8 != bytes_read ) {
    GST_ERROR("This IVF not contain 2 frames, won't continue decoding");
    return -1;
  }
  fseek(fp, 32, SEEK_SET);

  printf("first 2 frames timestamps are %ld, %ld\n", ts1, ts2);
  if ((ts2 - ts1) != 0 && ts_scaler_fromIVF_n != 0)
    fps = (float) ts_scaler_fromIVF_d / (ts_scaler_fromIVF_n * (ts2 - ts1));
  printf("IVF FPS = %.2f\n", fps);
}

int main(int argc, char **argv)
{
  gst_init(NULL, NULL);
  GMainLoop *loop;
  GstElement *appsrc;
  GstElement *waylandsink;
  GstElement *decodebin;
  GstElement *pipeline;
  int code_type = 0;
  char *stream_file;
  guint bus_watch_id;
  int width;
  int height;
  char in_caps[512] = {0,};

  if (argc != 5) {
    GST_ERROR ("error input argument passed, e.g. ./secureappsrc2"
      "<id (h264:1;h265:2;vp9:3)> <width> <height> <stream file path>");
    GST_ERROR ("example usage:");
    GST_ERROR ("./secureappsrc2 1 1920 1080 test.h264");
    return 0;
  } else {
    code_type = atoi(argv[1]);
    width = atoi(argv[2]);
    height = atoi(argv[3]);
    stream_file = argv[4];
    if (code_type > 4 || code_type < 1 || stream_file == NULL) {
      GST_ERROR (" code_type or stream file input error");
      return 0;
    } else {
      switch (code_type) {
        case 1:
          Read_Buffer = Read_Buffer_From_H264_Start_Code_File;
#ifdef SECURE_PLAYBACK
          snprintf(in_caps, sizeof(in_caps), "video/x-h264secure, width=(int)%d, height=(int)%d", width, height);
#else
          snprintf(in_caps, sizeof(in_caps), "video/x-h264, stream-format=(string)byte-stream, alignment=(string)au, \
            width=(int)%d, height=(int)%d, interlace-mode=(string)progressive, chroma-format=(string)4:2:0, \
            bit-depth-luma=(uint)8, bit-depth-chroma=(uint)8, parsed=(boolean)true", width, height);
#endif
          break;
        case 2:
          Read_Buffer = Read_Buffer_From_H265_Start_Code_File;
#ifdef SECURE_PLAYBACK
          snprintf(in_caps, sizeof(in_caps), "video/x-h265secure, width=(int)%d, height=(int)%d", width, height);
#else
          snprintf(in_caps, sizeof(in_caps), "video/x-h265, stream-format=(string)byte-stream, alignment=(string)au, \
            width=(int)%d, height=(int)%d, interlace-mode=(string)progressive, chroma-format=(string)4:2:0, \
            bit-depth-luma=(uint)8, bit-depth-chroma=(uint)8, parsed=(boolean)true", width, height); 
#endif
          break;
        case 3:
          Read_Buffer = Read_Buffer_From_Ivf_File;
#ifdef SECURE_PLAYBACK
          snprintf(in_caps, sizeof(in_caps), "video/x-vp9secure, width=(int)%d, height=(int)%d", width, height);
#else
          snprintf(in_caps, sizeof(in_caps), "video/x-vp9, stream-format=(string)byte-stream, alignment=(string)au, \
            width=(int)%d, height=(int)%d, interlace-mode=(string)progressive, chroma-format=(string)4:2:0, \
            bit-depth-luma=(uint)8, bit-depth-chroma=(uint)8, parsed=(boolean)true", width, height);
#endif
          break;
      }
    }
  }
  fp = fopen( stream_file , "r" );
  input_nonsecure_buffer = g_malloc0(MAX_INPUT_BUFFER_SIZE);
  secureappsrc *appsrc_struct = g_new0(secureappsrc, 1);
  g_mutex_init (&appsrc_struct->file_lock);
  g_mutex_init (&appsrc_struct->buf_lock);
  g_cond_init (&appsrc_struct->buf_cond);
  appsrc_struct->sec_buf_queue = g_queue_new ();

#ifdef SECURE_PLAYBACK
  appsrc_struct->crypto = (Crypto*)g_new0(Crypto, 1);
  crypto_init (appsrc_struct->crypto);
#endif

  gboolean rett = FALSE;

  loop = g_main_loop_new(NULL, FALSE);
  appsrc_struct->loop = loop;
  appsrc = gst_element_factory_make("appsrc", "appsrc");
  GstCaps *caps;

  caps = gst_caps_from_string (in_caps);
  g_object_set (appsrc, "caps", caps, NULL);
  gst_caps_unref (caps);

  waylandsink = gst_element_factory_make("waylandsink", "waylandsink");
#ifdef SECURE_PLAYBACK
  decodebin = gst_element_factory_make("decodebin", "decodebin");
#else
  if (code_type == 1)
  {
    decodebin = gst_element_factory_make("omxh264dec", "omxh264dec");
  }
  else if (code_type == 2)
  {
    decodebin = gst_element_factory_make("omxh265dec", "omxh265dec");
  }
  else if (code_type == 3)
  {
    decodebin = gst_element_factory_make("omxvp9dec", "omxvp9dec");
  }

  g_object_set (decodebin, "input-buffer-sharing", 1, NULL);
#endif
  pipeline = gst_pipeline_new("pipeline");

  gst_bin_add_many(GST_BIN(pipeline), appsrc, decodebin, waylandsink, NULL);
#ifdef SECURE_PLAYBACK
  gst_element_link_many(appsrc, decodebin, NULL);
#else
  gst_element_link_many(appsrc, decodebin, waylandsink, NULL);
#endif

  if (code_type == 3)
  {
    g_mutex_lock (&appsrc_struct->file_lock);
    Parse_Ivf_File();
    g_mutex_unlock (&appsrc_struct->file_lock);
  }
#ifdef SECURE_PLAYBACK
  g_signal_connect(G_OBJECT(decodebin), "pad-added", G_CALLBACK(onNewPad), waylandsink);
  g_signal_connect(GST_BIN(decodebin), "deep-element-added", G_CALLBACK(onOMXVideoDecCreated), appsrc_struct);
#endif

  g_signal_connect(G_OBJECT(appsrc), "need-data", G_CALLBACK(onNeedData), appsrc_struct);

  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  bus_watch_id = gst_bus_add_watch (bus, msg_handler, appsrc_struct);
  gst_object_unref(bus);

  gst_element_set_state(pipeline, GST_STATE_PLAYING);
  GST_DEBUG ("pipeline set playing");
  g_main_loop_run(loop);
  gst_element_set_state(pipeline, GST_STATE_NULL);
  GST_DEBUG ("pipeline set null");
  g_source_remove (bus_watch_id);

  g_main_loop_unref(loop);
  g_mutex_clear (&appsrc_struct->file_lock);
  g_mutex_clear (&appsrc_struct->buf_lock);
  g_cond_clear (&appsrc_struct->buf_cond);
  g_queue_free (appsrc_struct->sec_buf_queue);
#ifdef SECURE_PLAYBACK
  crypto_deinit (appsrc_struct->crypto);
  g_free (appsrc_struct->crypto);
#endif
  g_free(appsrc_struct);

  if (fp) {
    fclose(fp);
    fp = NULL;
  }

  if (input_nonsecure_buffer)  {
    g_free(input_nonsecure_buffer);
    input_nonsecure_buffer = NULL;
  }
  return 0;
}
