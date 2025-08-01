/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef SENSOR_LIBRARY_CLASS_HPP
#define SENSOR_LIBRARY_CLASS_HPP

#include <iostream>
#include <cinttypes>
#include <unistd.h>
#include <vector>
#include <condition_variable>
#include <mutex>
#include <csignal>
#include <cstdlib>
#include <thread>
#include <queue>
#include <atomic>
#include <cstdint>

#include "sns_direct_channel.pb.h"
#include "sns_suid.pb.h"
#include "sns_std_sensor.pb.h"
#include "sns_direct_channel.h"
#include "AEEStdErr.h"
#include "rpcmem.h"
#include "remote.h"
#include "sensors_timeutil.h"

using namespace std;

// Sensor event data
struct SensorData {
  std::string sensor_type;
  int data_size;
  std::vector<float> batch_data;
  uint64_t timestamp;
};

struct suid_info {
  uint64_t low;
  uint64_t high;
};

typedef struct sns_sensor_event {
  uint64_t timestamp;
  uint32_t message_id;
  uint32_t event_len;
} sns_sensor_event;

typedef enum sns_request_type {
  SNS_GENERIC_SUID = 0,
  SNS_GENERIC_ATTRIBUTES,
  SNS_GENERIC_SAMPLE,
} sns_request_type;

class Sensor {
public:
  Sensor (std::string sensor_type, int sample_rate, int type);
  ~Sensor ();
  std::shared_ptr<SensorData> GetSensorData ();
  bool StopStream();

private:
  void StartStream ();
  void sensor_stream_worker ();

  //Used internal
  void read_generic_data_event (sns_request_type event_type);
  void send_config_request (sns_request_type req_type);
  void get_suid ();
  void get_attributes ();

  //configuration parameters
  std::string sensor_data_type;
  int sample_rate_hz;
  int channel_type;
  int resample_type;
  int calibrated_type;

  std::thread worker_thread_;
  std::queue<std::shared_ptr<SensorData>> sensor_data_queue;
  std::mutex queue_mutex;
};
#endif //SENSOR_LIBRARY_CLASS_HPP
