/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <gst/gst.h>

#include "lib-sensor.h"
#include "lib-sensor-c-api.h"

#ifndef DIRECT_CHANNEL_SHARED_MEMORY_SIZE
#define DIRECT_CHANNEL_SHARED_MEMORY_SIZE (10 * 8000 * 104)
#endif

#define DEFAULT_SUIT_PARAM 12370169555311111083ull

char* shared_buff_ptr = nullptr;
bool is_handle_available = false;
int channel_handle;
int adjusted_sample_rate;
int total_sensors_found = 0;

remote_handle64 fastRPC_remote_handle = -1;
remote_handle64 remote_handle_fd = 0;

std::vector<suid_info> suid_info_list;
std::vector<int> supported_sample_rate;

// Used to retrieve sensor data
std::shared_ptr<SensorData> Sensor::GetSensorData ()
{
  // Lock to ensure queue access is thread-safe
  std::lock_guard<std::mutex> lock (queue_mutex);
  if (!sensor_data_queue.empty ()) {
    auto sensor_data = sensor_data_queue.front ();
    sensor_data_queue.pop ();
    return sensor_data;
  }

  // Return an nullptr if the queue is empty
  return nullptr;
}

void fastRPC_remote_handle_init ()
{
  if (is_handle_available == false) {
    int nErr = AEE_SUCCESS;
    string uri = "file:///libsns_direct_channel_skel.so?"
                 "sns_direct_channel_skel_handle_invoke"
                 "&_modver=1.0";
    remote_handle64 handle_l;

    //check for slpi or adsp
    struct stat sb;
    if (!stat ("/sys/kernel/boot_slpi", &sb)) {
      uri +="&_dom=sdsp";
      if (remote_handle64_open (ITRANSPORT_PREFIX
          "createstaticpd:sensorspd&_dom=sdsp", &remote_handle_fd)) {
        GST_ERROR ("failed to open remote handle for sensorspd - sdsp");
      }
    }
    else {
      uri +="&_dom=adsp";
      if (remote_handle64_open (ITRANSPORT_PREFIX
          "createstaticpd:sensorspd&_dom=adsp", &remote_handle_fd)) {
        GST_ERROR ("failed to open remote handle for sensorspd - adsp\n");
      }
    }
    if (AEE_SUCCESS == (nErr = sns_direct_channel_open (uri.c_str (),
        &handle_l))) {
      GST_DEBUG ("sns_direct_channel_open"
          "success for sensorspd - handle_l is %ud\n", (unsigned int) handle_l);
      fastRPC_remote_handle = handle_l;
      is_handle_available = true;
    } else {
      fastRPC_remote_handle = -1;
    }
  }
  GST_DEBUG ("get_fastRPC_remote_handle End");
}

int create_rpc_memory ()
{
  shared_buff_ptr = (char*) rpcmem_alloc (RPCMEM_HEAP_ID_SYSTEM,
      RPCMEM_DEFAULT_FLAGS, DIRECT_CHANNEL_SHARED_MEMORY_SIZE);
  if (NULL == shared_buff_ptr) {
    GST_ERROR ("open_new_direct_channel: rpcmem_alloc failed \n");
    return -1;
  }

  GST_DEBUG ("open_new_direct_channel: rpcmem_alloc success %p \n",
      shared_buff_ptr);
  return rpcmem_to_fd ((void *) shared_buff_ptr);
}

void delete_direct_channel ()
{
  if (sns_direct_channel_delete (fastRPC_remote_handle, channel_handle) == 0) {
    GST_DEBUG ("delete_direct_channel pass  \n");
  } else {
    GST_ERROR ("delete_direct_channel fail \n");
  }

  channel_handle = -1;
  rpcmem_free ((void*) shared_buff_ptr);
}

void create_direct_channel (bool is_generic_channel)
{
  int fd = create_rpc_memory ();
  if (fd < 0) {
    GST_ERROR ("Failed to create RPC memory");
    return;
  }

  sns_direct_channel_create_msg create_msg;
  sns_direct_channel_create_msg_shared_buffer_config *shared_buffer_config =
      create_msg.mutable_buffer_config ();

  GST_DEBUG ("open_new_direct_channel: fd is %d for channel_type %d \n",
      fd , is_generic_channel);

  if (NULL == shared_buffer_config) {
    GST_ERROR ("Failed to create RPC memory");
    return;
  }

  GST_DEBUG ("open_new_direct_channel: shared_buffer_config not null \n");
  shared_buffer_config->set_fd (fd);
  shared_buffer_config->set_size (DIRECT_CHANNEL_SHARED_MEMORY_SIZE);

  if (true == is_generic_channel)
    create_msg.set_channel_type (DIRECT_CHANNEL_TYPE_GENERIC_CHANNEL);
  else
    create_msg.set_channel_type (DIRECT_CHANNEL_TYPE_STRUCTURED_MUX_CHANNEL);

  create_msg.set_client_proc (SNS_STD_CLIENT_PROCESSOR_APSS);
  string pb_encoded_direct_channel_req_msg;
  create_msg.SerializeToString (&pb_encoded_direct_channel_req_msg);
  GST_DEBUG ("open_new_direct_channel: before sns_direct_channel_create  \n");

  int ret = sns_direct_channel_create (fastRPC_remote_handle,
      (const unsigned char*) pb_encoded_direct_channel_req_msg.c_str(),
      pb_encoded_direct_channel_req_msg.size(), &channel_handle);

  if (0 == ret)
    GST_DEBUG ("sensor_direct_channel sns_direct_channel_create success,"
        "and channel_handle %d \n ", channel_handle);
  else
    GST_ERROR ("sensor_direct_channel sns_direct_channel_create failed \n");
}

void Sensor::send_config_request (sns_request_type req_type)
{
  sns_direct_channel_config_msg config_msg;
  sns_direct_channel_set_client_req* req_msg = config_msg.mutable_set_client_req();
  if (NULL == req_msg)
    return;

  if (req_type == SNS_GENERIC_SUID)
    req_msg->set_msg_id (SNS_SUID_MSGID_SNS_SUID_REQ);
  else if (req_type == SNS_GENERIC_ATTRIBUTES)
    req_msg->set_msg_id (SNS_STD_MSGID_SNS_STD_ATTR_REQ);
  else if (req_type == SNS_GENERIC_SAMPLE)
    req_msg->set_msg_id (SNS_STD_SENSOR_MSGID_SNS_STD_SENSOR_CONFIG);

  sns_direct_channel_stream_id* stream_id_msg = req_msg->mutable_stream_id ();
  if (NULL == stream_id_msg)
    return;

  sns_std_suid *suid = stream_id_msg->mutable_suid();
  if (NULL == suid)
    return;

  if (req_type == SNS_GENERIC_SUID) {
    suid->set_suid_low (DEFAULT_SUIT_PARAM);
    suid->set_suid_high (DEFAULT_SUIT_PARAM);
  }
  if (req_type == SNS_GENERIC_ATTRIBUTES || req_type == SNS_GENERIC_SAMPLE) {
    suid->set_suid_low (suid_info_list.at (0).low);
    suid->set_suid_high (suid_info_list.at (0).high);
  }

  sns_std_request* std_req_msg = req_msg->mutable_request();
  if (NULL == std_req_msg)
    return;

  if (req_type == SNS_GENERIC_SAMPLE) {
    if (0 == calibrated_type)
      stream_id_msg->set_calibrated (false);
    else
      stream_id_msg->set_calibrated (true);
    if (0 == resample_type)
      stream_id_msg->set_resampled (false);
    else
      stream_id_msg->set_resampled (true);
  } else {
    stream_id_msg->set_calibrated (false);
    stream_id_msg->set_resampled (false);
  }

  string payload = "";
  if (req_type == SNS_GENERIC_SUID) {
    std::string s_name (sensor_data_type);
    sns_suid_req reg;
    reg.set_data_type (s_name);
    reg.SerializeToString (&payload);
  }
  if (req_type == SNS_GENERIC_ATTRIBUTES) {
    /*This is something like on change sensor. It doesn't have any message
      and corresponding payload. So this should be empty*/
  }
  if (req_type == SNS_GENERIC_SAMPLE) {
    sns_std_sensor_config std_req;
    std_req.set_sample_rate (float (sample_rate_hz));
    std_req.SerializeToString (&payload);
  }
  std_req_msg->set_payload (payload);
  string pb_encoded_direct_channel_config_msg;
  config_msg.SerializeToString (&pb_encoded_direct_channel_config_msg);
  GST_DEBUG ("pb_encoded_direct_channel_config_msg"
      "string is %s and size is %ld \n",
      pb_encoded_direct_channel_config_msg.c_str (),
      pb_encoded_direct_channel_config_msg.size ());

  int ret =sns_direct_channel_config (fastRPC_remote_handle, channel_handle,
      (const unsigned char*) pb_encoded_direct_channel_config_msg.c_str (),
      pb_encoded_direct_channel_config_msg.size ());
  if (0 == ret)
    GST_DEBUG ("sensor_direct_channel send_request success \n");
  else
    GST_ERROR ("sensor_direct_channel send_request failed \n");
}

void sr_based_sleep () {
  usleep (1000000 / adjusted_sample_rate);
}

void Sensor::read_generic_data_event (sns_request_type event_type)
{
  char* temp = shared_buff_ptr;
  char *end_ptr = shared_buff_ptr + DIRECT_CHANNEL_SHARED_MEMORY_SIZE;
  unsigned long long previous_timestamp = 0;
  (void) previous_timestamp;

  while(1) {
    char* pckt_header_start_ptr = temp;
    char *pckt_header_end_ptr = pckt_header_start_ptr + sizeof (sns_sensor_event);
    if (pckt_header_end_ptr > end_ptr) {
      GST_DEBUG ("\n\n Header itself not fit. So moving to start of the buffer."
          "pckt_header_start_ptr %p , pckt_header_end_ptr %p , end_ptr %p \n",
          pckt_header_start_ptr , pckt_header_end_ptr, end_ptr);
      temp = shared_buff_ptr;
      continue;
    } else {
      if (channel_handle == -1)
        break;
      sns_sensor_event event = *reinterpret_cast<const sns_sensor_event *>(temp);
      if (0 == event.timestamp) {
        sr_based_sleep();
      } else {
        char *pckt_payload_start_ptr = pckt_header_end_ptr ;
        char *pckt_payload_end_ptr = pckt_header_end_ptr +  event.event_len;
        if (pckt_payload_end_ptr > end_ptr) {
          GST_DEBUG ("\npayload not fit. So read the header from"
              "last and reading the payload from start. \n");
          GST_DEBUG ("pckt_header_start_ptr %p, pckt_header_end_ptr %p,"
              "end_ptr %p \n", pckt_header_start_ptr,
              pckt_header_end_ptr, end_ptr);
          GST_DEBUG ("pckt_payload_start_ptr %p, pckt_payload_end_ptr %p,"
              "end_ptr %p \n", pckt_payload_start_ptr,
              pckt_payload_end_ptr, end_ptr);
          GST_DEBUG ("Circular buffer overflow scenario - Headers fits"
              "but not payload with length %d with message id as %d  \n",
              event.event_len, (int) event.message_id);
          pckt_payload_start_ptr = shared_buff_ptr;
          pckt_payload_end_ptr = pckt_payload_start_ptr + event.event_len;
        }
        // SUID Event decoding
        if (event_type == SNS_GENERIC_SUID) {
          if (event.message_id == SNS_SUID_MSGID_SNS_SUID_EVENT) {
            sns_suid_event suid_event_data;
            suid_event_data.ParseFromArray (pckt_payload_start_ptr,
                event.event_len);
            string current_data_type = suid_event_data.data_type ();
            int suid_count = suid_event_data.suid_size ();
            total_sensors_found = suid_count;
            GST_DEBUG ("suid_count is %d \n" , suid_count);

            for (int i =0 ; i < suid_count ; i++) {
              suid_info suid;
              suid.low = suid_event_data.suid (i).suid_low ();
              suid.high = suid_event_data.suid (i).suid_high ();
              suid_info_list.push_back (suid);
            }
            break;
          }
        }

        // Attribute Event decoding
        if (event_type == SNS_GENERIC_ATTRIBUTES) {
          if (event.message_id == SNS_STD_MSGID_SNS_STD_ATTR_EVENT) {
            sns_std_attr_event attr_event_data;
            attr_event_data.ParseFromArray (pckt_payload_start_ptr, event.event_len);
            int attr_count = attr_event_data.attributes_size ();
            for (int i = 0 ; i < attr_count ; i++) {
              GST_DEBUG ("attribute count %d \t and values are: ", i);
              sns_std_attr attr = attr_event_data.attributes (i);
              GST_DEBUG ("attr_id: %d \t " , attr.attr_id ());
              int current_attrib_id = attr.attr_id ();
              sns_std_attr_value attr_value = attr.value ();
              int attr_value_count = attr_value.values_size ();
                for (int i = 0; i < attr_value_count ; i ++) {
                  sns_std_attr_value_data val = attr_value.values (i);
                  if (val.has_flt ()) {
                    GST_DEBUG ("flt: %f" , val.flt());
                    /*Storting supported sample rates in hz in the form of vector*/
                    if (current_attrib_id == SNS_STD_SENSOR_ATTRID_RATES) {
                      supported_sample_rate.push_back ((int) val.flt ());
                    }
                  } else if (val.has_sint()){
                    GST_DEBUG ("sint: %ld" , val.sint ());
                  } else if (val.has_boolean()) {
                    GST_DEBUG ("boolean %d ", (int) val.boolean ());
                  } else if (val.has_str()) {
                    GST_DEBUG ("std: %s " , val.str ().c_str ());
                  }
                }
              GST_DEBUG ("\n");
            }
            break;
          }
        }

        // Sensor data event parsing.
        if (event_type == SNS_GENERIC_SAMPLE) {
          if ( SNS_STD_SENSOR_MSGID_SNS_STD_SENSOR_EVENT == event.message_id ) {
            sns_std_sensor_event std_sensor_data;
            std_sensor_data.ParseFromArray (pckt_payload_start_ptr,
                event.event_len);
            std::vector<float> sensor_samples;
            for (int i = 0 ; i < std_sensor_data.data_size () ; i++) {
              sensor_samples.push_back (std_sensor_data.data (i));
            }

            auto sensor_data = std::make_shared<SensorData> ();
            sensor_data->sensor_type = sensor_data_type;
            sensor_data->data_size = std_sensor_data.data_size ();
            sensor_data->batch_data = std::move (sensor_samples);
            sensor_data->timestamp = event.timestamp;

            // Push the parsed samples into the queue.
            std::lock_guard<std::mutex> lock (queue_mutex);
            sensor_data_queue.push (std::move (sensor_data));
          }
          GST_DEBUG ("\n");
          total_sensors_found ++;
          previous_timestamp = event.timestamp;
          temp = pckt_payload_end_ptr;
          if (temp >= end_ptr) {
            GST_DEBUG ("looping to beginning of the buffer \n");
            temp = shared_buff_ptr;
          }
        }
      }
    }
  }
}

void Sensor::get_suid ()
{
  GST_DEBUG ("get_suid started for sensor %s \n", sensor_data_type.c_str());
  create_direct_channel (true);
  send_config_request (SNS_GENERIC_SUID);
  read_generic_data_event (SNS_GENERIC_SUID);

  GST_DEBUG ("SUID event received and total Number of sensors Found are %d \n",
      total_sensors_found);
  for(int i = 0 ; i < total_sensors_found ; i ++) {
    GST_DEBUG ("sensor[%d]: suid_low:%lu , suid_high:%lu \n", i,
        suid_info_list.at (i).low , suid_info_list.at (i).high);
  }

  delete_direct_channel ();
  GST_DEBUG ("get_suid_list Ended for sensor %s \n", sensor_data_type.c_str ());
}

void Sensor::get_attributes ()
{
  create_direct_channel (true);
  send_config_request (SNS_GENERIC_ATTRIBUTES);
  read_generic_data_event (SNS_GENERIC_ATTRIBUTES);
  delete_direct_channel ();
}

void set_ts_offset (int channel_handle)
{
  int64_t ts_offset;
  sensors_timeutil& timeutil = sensors_timeutil::get_instance ();
  timeutil.recalculate_offset (true);
  ts_offset = timeutil.getElapsedRealtimeNanoOffset ();
  sns_direct_channel_config_msg config_msg;
  sns_direct_channel_set_ts_offset *offset_msg = config_msg.mutable_set_ts_offset ();
  if (NULL == offset_msg)
    return;

  offset_msg->set_ts_offset(ts_offset);
  GST_DEBUG ("sensor_direct_channel current offset is %d \n", (int) ts_offset);
  string pb_encoded_direct_channel_config_msg;
  config_msg.SerializeToString (&pb_encoded_direct_channel_config_msg);
  int ret = sns_direct_channel_config (fastRPC_remote_handle, channel_handle,
      (const unsigned char*) pb_encoded_direct_channel_config_msg.c_str (),
      pb_encoded_direct_channel_config_msg.size ());
  if (0 == ret)
    GST_DEBUG ("sensor_direct_channel set_ts_offset success \n");
  else
    GST_ERROR ("sensor_direct_channel set_ts_offset failed \n");
}

int stop_sensor_streaming (int calibrated_type, bool resample_type,
    vector<suid_info> suid_info_list)
{
  string pb_encoded_direct_channel_config_msg;
  sns_direct_channel_config_msg config_msg;
  sns_direct_channel_remove_client_req
      *rmv_req = config_msg.mutable_remove_client_req ();

  if (NULL == rmv_req)
    return 0;
  sns_direct_channel_stream_id
      *stream_id_msg = rmv_req->mutable_stream_id ();

  if (NULL == stream_id_msg)
    return 0;

  sns_std_suid *suid = stream_id_msg->mutable_suid ();
  if (NULL == suid)
    return 0;

  suid->set_suid_low(suid_info_list.at (0).low);
  suid->set_suid_high(suid_info_list.at (0).high);

  if(0 == calibrated_type)
    stream_id_msg->set_calibrated (false);
  else
    stream_id_msg->set_calibrated (true);
  if(0 == resample_type)
    stream_id_msg->set_resampled (false);
  else
    stream_id_msg->set_resampled (true);

  config_msg.SerializeToString (&pb_encoded_direct_channel_config_msg);
  int ret = sns_direct_channel_config (fastRPC_remote_handle, channel_handle,
          (const unsigned char*) pb_encoded_direct_channel_config_msg.c_str (),
          pb_encoded_direct_channel_config_msg.size ());
  if (0 == ret) {
    GST_DEBUG ("stop_streaming success \n");
  } else {
    GST_ERROR ("stop_streaming failed \n");
  }
  delete_direct_channel ();
  return 0;
}

void caluclate_adjusted_sample_rate (int sample_rate_hz, bool resample_type)
{
  int asked = sample_rate_hz;
  int adjusted =  -1;
  if (false == resample_type) {
    adjusted = asked;
  } else {
    for (unsigned int i =0 ; i < supported_sample_rate.size (); i ++) {
      if (supported_sample_rate.at (i) < asked ) {
        continue;
      } else {
        adjusted = supported_sample_rate.at (i);
        break;
      }
    }
    if (adjusted == -1 ) {
      adjusted = asked;
    }
  }
  GST_DEBUG ("\n Resample Status: %d,\tSR Asked for %d,\tSR adjusted %d \n",
      resample_type, asked, adjusted);
  adjusted_sample_rate = adjusted;
}

void resource_cleanup ()
{
  if (0 != fastRPC_remote_handle)
    sns_direct_channel_close (fastRPC_remote_handle);
  if (0 != remote_handle_fd)
    remote_handle64_close (remote_handle_fd);
}

bool Sensor::StopStream ()
{
  int res = stop_sensor_streaming (0, 0, suid_info_list);
  if (worker_thread_.joinable ())
    worker_thread_.join ();

  if (res != 0)
    return false;

  return true;
}

// Start streaming
void Sensor::StartStream ()
{
  create_direct_channel (true);
  set_ts_offset (channel_handle);
  send_config_request (SNS_GENERIC_SAMPLE);
  read_generic_data_event (SNS_GENERIC_SAMPLE);
}

// Active for streaming to work, so we run this in a thread
void Sensor::sensor_stream_worker()
{
  StartStream ();
}

Sensor::Sensor (std::string sensor_type, int sample_rate, int type)
  : sensor_data_type (sensor_type),
    sample_rate_hz (sample_rate),
    channel_type (type)
{
  //Step1. Init fastRPC.
  fastRPC_remote_handle_init ();

  //Step2. Get suid and attribute.
  get_suid ();
  get_attributes ();

  //Step3. Adjust sample rate.
  caluclate_adjusted_sample_rate (sample_rate_hz, 0);

  //Step4. Should call start stream here in a thread
  worker_thread_ = std::thread (&Sensor::sensor_stream_worker, this);
}

Sensor::~Sensor ()
{
  resource_cleanup ();
}

// Used for C-API
void* create_sensor (const char* sname, int32_t sr, int32_t ct) {
    return static_cast<void*>(new Sensor (sname, sr, ct));
}

CSensorData* get_sensor_data (void* sensor_ptr)
{
  CSensorData* c_data = nullptr;

  try {
    Sensor* sensor = static_cast<Sensor*>(sensor_ptr);
    if (!sensor)
      return nullptr;

    auto cpp_data = sensor->GetSensorData ();
    if (!cpp_data)
      return nullptr;

    c_data = new CSensorData ();
    std::strncpy (c_data->sensor_type, cpp_data->sensor_type.c_str (),
        sizeof (c_data->sensor_type) - 1);

    // ensure null-termination
    c_data->sensor_type[sizeof (c_data->sensor_type) - 1] = '\0';
    c_data->data_size = cpp_data->data_size;
    c_data->timestamp = cpp_data->timestamp;

    // First calculate how many elements we can safely copy
    uint32_t copy_size = std::min (cpp_data->batch_data.size (), (size_t) 128);
    c_data->batch_data_size = copy_size;

    // Ensure data_size is also within bounds
    c_data->data_size = std::min (cpp_data->data_size, (int32_t) copy_size);

    for (uint32_t i = 0; i < copy_size; ++i) {
      c_data->batch_data[i] = cpp_data->batch_data[i];
    }

    return c_data;

  } catch (const std::exception& e) {
    std::cerr << "Exception in get_sensor_data: " << e.what () << std::endl;
    if (c_data) delete c_data;
    return nullptr;
  } catch (...) {
    std::cerr << "Unknown exception in get_sensor_data" << std::endl;
    if (c_data) delete c_data;
    return nullptr;
  }
}

int32_t stop_stream (void* sensor_ptr)
{
  try {
    if (!sensor_ptr)
      return 0;

    Sensor* sensor = static_cast<Sensor*>(sensor_ptr);
    return sensor->StopStream() ? 1 : 0;

  } catch (const std::exception& e) {
    std::cerr << "Exception in stop_stream: " << e.what () << std::endl;
    return 0;
  } catch (...) {
    std::cerr << "Unknown exception in stop_stream" << std::endl;
    return 0;
  }
}

void destroy_sensor (void* sensor_ptr)
{
    Sensor* sensor = static_cast<Sensor*>(sensor_ptr);
    delete sensor;
}

void destroy_sensor_data (CSensorData* data)
{
  if (data) {
    delete data;
  }
}
