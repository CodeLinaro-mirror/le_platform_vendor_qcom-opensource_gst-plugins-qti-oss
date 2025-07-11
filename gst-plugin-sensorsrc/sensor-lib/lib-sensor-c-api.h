/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// C-compatible version of SensorData
typedef struct {
  char sensor_type[32];
  int32_t data_size;
  float batch_data[128];
  int32_t batch_data_size;
  unsigned long long timestamp;
} CSensorData;

typedef void* (*create_sensor_fn) (const char*, int32_t, int32_t);
typedef void (*destroy_sensor_fn) (void*);
typedef CSensorData* (*get_sensor_data_fn) (void*);
typedef void (*destroy_sensor_data_fn) (CSensorData*);
typedef int32_t (*stop_stream_fn) (void*);

void* create_sensor (const char* sname, int32_t sample_rate, int32_t batch_period);

CSensorData* get_sensor_data (void* sensor_ptr);

int32_t stop_stream (void* sensor_ptr);

// Destroy the Sensor object
void destroy_sensor (void* sensor_ptr);

// Clean up the CSensorData object returned from get_sensor_data
void destroy_sensor_data (CSensorData* data);

#ifdef __cplusplus
}
#endif
