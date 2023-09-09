/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
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
 * (IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef __GST_QEAVB_COMMON_H__
#define __GST_QEAVB_COMMON_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <time.h>
#include "qavblib.h"

#define DEFALUT_SLEEP_US 10000
#define MIN_SLEEP_US 2000
#define MIN_RETRY_TOTALTIME_US  250000000
#define RETRY_COUNT (MIN_RETRY_TOTALTIME_US/MIN_SLEEP_US)  //When retry sleep time equal to MIN_SLEEP_US, total retry time equal to MIN_RETRY_TOTALTIME_US

#define LOG_BEATHEAT_EXPECTED_PERIOD_NS 2000000000LL  //2 seconds

typedef struct {
  int counter;
  int period;
  struct timespec last_t;
  int last_t_valid;
  int period_upper;
  int period_lower;
}LOG_HEARTBEAT_CTX;

int qeavb_create_stream_remote(int eavb_fd, char* file_path, eavb_ioctl_hdr_t* hdr);
int qeavb_get_stream_info(int eavb_fd, eavb_ioctl_hdr_t* hdr, eavb_ioctl_stream_info_t* info);
int qeavb_destroy_stream(int eavb_fd, eavb_ioctl_hdr_t* hdr);
int qeavb_connect_stream(int eavb_fd, eavb_ioctl_hdr_t* hdr);
int qeavb_disconnect_stream(int eavb_fd, eavb_ioctl_hdr_t* hdr);
int qeavb_receive_data(int eavb_fd, eavb_ioctl_hdr_t* hdr, eavb_ioctl_buf_data_t* buff);
int qeavb_receive_done(int eavb_fd, eavb_ioctl_hdr_t* hdr, eavb_ioctl_buf_data_t* data);
int kpi_place_marker(const char* str);
int log_heartbeat_init(LOG_HEARTBEAT_CTX* ctx, int period_init, int period_min, int period_max);
int log_heartbeat_counter_reset(LOG_HEARTBEAT_CTX* ctx);
int log_heartbeat_counter_click(LOG_HEARTBEAT_CTX* ctx);

#endif /* __GST_QEAVB_COMMON_H__ */

