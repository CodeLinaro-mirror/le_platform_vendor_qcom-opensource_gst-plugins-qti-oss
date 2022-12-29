/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright 2020-2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <mutex>
#include <iostream>
#include <chrono>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>
#include <rapidjson/error/en.h>

#include "common.h"
#include "grpc_client.h"
#include "http_client.h"
#include "shm_utils.h"
#include "tritoninfer.h"
#include "labels/label_mark.h"

namespace tc = triton::client;

#define RGB_CHENNEL_NUM 3
#define EXTRACT_RED_COLOR(color)   ((color >> 24) & 0xFF)
#define EXTRACT_GREEN_COLOR(color) ((color >> 16) & 0xFF)
#define EXTRACT_BLUE_COLOR(color)  ((color >> 8) & 0xFF)
#define EXTRACT_ALPHA_COLOR(color) ((color) & 0xFF)

#define FAIL_IF_ERR(RES, MSG)                                      \
  {                                                                \
    tc::Error err = (RES);                                         \
    if (!err.IsOk()) {                                             \
      std::cerr << "error: " << (MSG) << ": " << err << std::endl; \
      exit(1);                                                     \
    }                                                              \
  }

enum
{
  BATCH_SIZE,
  HEIGHT,
  WIDTH,
  CHANNEL,
};

typedef struct
{
  std::string name;
  cv::Scalar color;
} label_info;

typedef struct
{
  std::vector<std::string> input_names;
  std::vector<int64_t> input_shape;
  std::vector<std::string> output_names;
  std::vector<int64_t> output_sizes;
  std::vector<label_info> labels;
  std::string input_datatype;
} model_info;

typedef struct
{
  float *output;
  size_t output_bytesize;
  float *num;
  size_t num_bytesize;
} result_info;

union TritonClient {
  TritonClient()
  {
    new (&http_client_) std::unique_ptr<tc::InferenceServerHttpClient>{};
  }
  ~TritonClient() {}

  std::unique_ptr<tc::InferenceServerHttpClient> http_client_;
  std::unique_ptr<tc::InferenceServerGrpcClient> grpc_client_;
};

gboolean
create_input (GstTriton *triton)
{
  tc::InferInput* input;
  tc::Error err;
  model_info *info = (model_info *) triton->model_info;
  std::string datatype = info->input_datatype;
  err = tc::InferInput::Create(&input, info->input_names[0], info->input_shape, datatype);
  if (!err.IsOk()) {
    GST_ERROR_OBJECT (triton, "unable to get input: %s!", err.Message().c_str());
    return FALSE;
  }

  ((std::vector<tc::InferInput*> *)(triton->infer_inputs))->push_back(input);
  if (triton->shm_key == NULL) {
    err = input->AppendRaw((const uint8_t* )(triton->input_buf->buf), triton->input_buf->size);
    if (!err.IsOk()) {
      GST_ERROR_OBJECT (triton, "failed setting input: %s!", err.Message().c_str());
      return FALSE;
    }
  }
  return TRUE;
}

void
register_share_memory (GstTriton *triton)
{
  TritonClient *triton_client = (TritonClient *)triton->client;
  model_info *info = (model_info *) triton->model_info;
  std::string datatype = info->input_datatype;
  std::vector<std::string> output_names = info->output_names;
  std::vector<int64_t> output_sizes = info->output_sizes;
  tc::InferInput* input;

  int shm_fd_ip;
  std::string suffix = std::string(triton->shm_key);
  std::string input_shm_key = "/input" + suffix;
  std::string register_input_name = info->input_names[0] + suffix;
  size_t input_byte_size = triton->input_buf->size;

  // Great input share memory
  FAIL_IF_ERR(
    tc::CreateSharedMemoryRegion(input_shm_key, input_byte_size, &shm_fd_ip)
    , "");

  FAIL_IF_ERR(
      tc::MapSharedMemory(
          shm_fd_ip, 0, input_byte_size, (void**) &(triton->input_buf->buf)),
      "");
  FAIL_IF_ERR(tc::CloseSharedMemory(shm_fd_ip), "");

  if (triton->infer_mode == HTTP_MODE) {
    FAIL_IF_ERR(
      triton_client->http_client_->UnregisterSystemSharedMemory(register_input_name),
        "unable to unregister shared memory input region");
    FAIL_IF_ERR(
        triton_client->http_client_->RegisterSystemSharedMemory(
            register_input_name, input_shm_key, input_byte_size),
        "failed to register input shared memory region");
  } else if (triton->infer_mode == GRPC_MODE) {
    FAIL_IF_ERR(
      triton_client->grpc_client_->UnregisterSystemSharedMemory(register_input_name),
        "unable to unregister shared memory input region");
    FAIL_IF_ERR(
        triton_client->grpc_client_->RegisterSystemSharedMemory(
            register_input_name, input_shm_key, input_byte_size),
        "failed to register input shared memory region");
  }

  FAIL_IF_ERR(
      tc::InferInput::Create(&input, info->input_names[0], info->input_shape, datatype),
      "unable to creat input");

  FAIL_IF_ERR(
      input->SetSharedMemory(
          register_input_name, triton->input_buf->size, 0 /* offset */),
          "unable to set shared memory for INPUT0");

  ((std::vector<tc::InferInput*> *)(triton->infer_inputs))->push_back(input);
}

void
unregister_share_memory (GstTriton *triton)
{
  TritonClient *triton_client = (TritonClient *)triton->client;
  model_info *info = (model_info *) triton->model_info;
  std::string suffix = std::string(triton->shm_key);
  std::string input_shm_key = "/input" + suffix;
  std::string register_input_name = info->input_names[0] + suffix;
  size_t input_byte_size = triton->input_buf->size;

  // Unregister shared memory
  if (triton->infer_mode == HTTP_MODE) {
    FAIL_IF_ERR(
        triton_client->http_client_->UnregisterSystemSharedMemory(register_input_name),
        "unable to unregister shared memory input region");
  } else if (triton->infer_mode == GRPC_MODE) {
    FAIL_IF_ERR(
        triton_client->grpc_client_->UnregisterSystemSharedMemory(register_input_name),
        "unable to unregister shared memory input region");
  }

  // Cleanup shared memory
  FAIL_IF_ERR(tc::UnmapSharedMemory(triton->input_buf->buf, input_byte_size), "");
  FAIL_IF_ERR(tc::UnlinkSharedMemoryRegion(input_shm_key), "");
}

gint
calcuate_border (gint src_w, gint src_h, gint dst_w, gint dst_h,
    gint *lr_pad, gint *tb_pad, gint *img_width, gint *img_height) {

    if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        std::cout << "calcuate_border failed due to incorrect size!" << std::endl;
        return -1;
    }

    if ((gfloat)src_w / src_h == (gfloat)dst_w / dst_h) {
        *img_width = dst_w;
        *img_height = dst_h;
        *lr_pad = 0;
        *tb_pad = 0;
    } else if((gfloat)src_w / src_h > (gfloat)dst_w / dst_h){
        gint resize_h = dst_w * (gfloat)src_h / src_w;
        if (resize_h % 2 != 0)
            resize_h = (resize_h < dst_h) ? (resize_h + 1) : (resize_h - 1);
        gint fill_h = (dst_h - resize_h) / 2;
        *img_width = dst_w;
        *img_height = resize_h;
        *lr_pad = 0;
        *tb_pad = fill_h;
    } else {
        gint resize_w = dst_h * (gfloat)src_w / src_h;
        if (resize_w % 2 != 0)
            resize_w = (resize_w < dst_w) ? (resize_w + 1) : (resize_w - 1);
        gint fill_w = (dst_w - resize_w) / 2;
        *img_width = resize_w;
        *img_height = dst_h;
        *lr_pad = fill_w;
        *tb_pad = 0;
    }
    return 0;
}

gboolean
gst_load_labels (const gchar * input, std::vector<label_info> &labels)
{
  GValue list = G_VALUE_INIT;
  guint idx = 0, id = 0, color=0, label_size = 0;
  gchar* name;

  g_return_val_if_fail (input != NULL, FALSE);
  g_value_init (&list, GST_TYPE_LIST);

  if (g_file_test (input, G_FILE_TEST_IS_REGULAR)) {
    GString *string = NULL;
    GError *error = NULL;
    gchar *contents = NULL;
    gboolean success = FALSE;

    if (!g_file_get_contents (input, &contents, NULL, &error)) {
      GST_ERROR ("Failed to get labels file contents, error: %s!",
          GST_STR_NULL (error->message));
      g_clear_error (&error);
      return FALSE;
    }

    // Remove trailing space and replace new lines with a comma delimiter.
    contents = g_strstrip (contents);
    contents = g_strdelimit (contents, "\n", ',');

    string = g_string_new (contents);
    g_free (contents);

    // Add opening and closing brackets.
    string = g_string_prepend (string, "{ ");
    string = g_string_append (string, " }");

    // Get the raw character data.
    contents = g_string_free (string, FALSE);

    success = gst_value_deserialize (&list, contents);
    g_free (contents);

    if (!success) {
      GST_ERROR ("Failed to deserialize labels file contents!");
      return FALSE;
    }
  } else if (!gst_value_deserialize (&list, input)) {
    GST_ERROR ("Failed to deserialize labels!");
    return FALSE;
  }

  label_size = gst_value_list_get_size (&list);
  labels.resize(label_size);
  for (idx = 0; idx < label_size; idx++) {
    GstStructure *structure = NULL;

    structure = GST_STRUCTURE (
        g_value_get_boxed (gst_value_list_get_value (&list, idx)));

    if (structure == NULL) {
      GST_WARNING ("Failed to extract structure!");
      continue;
    } else if (!gst_structure_has_field (structure, "id") ||
        !gst_structure_has_field (structure, "color")) {
      GST_WARNING ("Structure does not contain 'id' and/or 'color' fields!");
      continue;
    }

    gst_structure_get_uint (structure, "color", &color);
    gst_structure_get_uint (structure, "id", &id);
    name = g_strdup (gst_structure_get_name (structure));
    name = g_strdelimit (name,  "-", ' ');
    labels[id] = {std::string(name),
      cv::Scalar(EXTRACT_RED_COLOR(color),
                 EXTRACT_GREEN_COLOR(color),
                 EXTRACT_BLUE_COLOR(color),
                 EXTRACT_ALPHA_COLOR(color))};
  }
  return TRUE;
}

tc::Error
parse_json (rapidjson::Document* document, const std::string& json_str)
{
  const unsigned int parseFlags = rapidjson::kParseNanAndInfFlag;
  document->Parse<parseFlags>(json_str.c_str(), json_str.size());
  if (document->HasParseError()) {
    return tc::Error(
        "failed to parse JSON at" + std::to_string(document->GetErrorOffset()) +
        ": " + std::string(GetParseError_En(document->GetParseError())));
  }

  return tc::Error::Success;
}

void parse_model_http (const rapidjson::Document& model_metadata,
    const rapidjson::Document& model_config, model_info* info)
{
  const auto& input_itr = model_metadata.FindMember("inputs");
  size_t input_count = 0;
  if (input_itr != model_metadata.MemberEnd()) {
    input_count = input_itr->value.Size();
  }
  if (input_count != 1) {
    std::cerr << "expecting 1 input, got " << input_count << std::endl;
    exit(1);
  }

  const auto& input_config_itr = model_config.FindMember("input");
  input_count = 0;
  if (input_config_itr != model_config.MemberEnd()) {
    input_count = input_config_itr->value.Size();
  }
  if (input_count != 1) {
    std::cerr << "expecting 1 input in model configuration, got " << input_count
              << std::endl;
    exit(1);
  }

  const auto& input_metadata = *input_itr->value.Begin();
  const auto& output_itr = model_metadata.FindMember("outputs");
  info->input_names.push_back(input_metadata["name"].GetString());

  const auto& input_dtype_itr = input_metadata.FindMember("datatype");
  if (input_dtype_itr == input_metadata.MemberEnd()) {
    std::cerr << "input missing datatype in the metadata for model'"
              << model_metadata["name"].GetString() << "'" << std::endl;
    exit(1);
  }

  auto datatype = std::string(
      input_dtype_itr->value.GetString(),
      input_dtype_itr->value.GetStringLength());

  if (datatype.compare("UINT8") != 0 && datatype.compare("INT8") != 0) {
    std::cerr << "expecting input datatype to be UINT8/INT8, model '"
              << model_metadata["name"].GetString() << "' output type is '"
              << datatype << "'" << std::endl;
    exit(1);
  } else {
    info->input_datatype = datatype;
  }

  for(unsigned int i=0; i < output_itr->value.Size(); i++)
  {
    info->output_names.push_back(output_itr->value[i]["name"].GetString());
  }

  const auto input_shape_itr = input_metadata.FindMember("shape");
  for(unsigned int n=0; n<input_shape_itr->value.Size(); n++)
  {
    info->input_shape.push_back(input_shape_itr->value[n].GetInt());
  }

}

void parse_model_grpc (GstTriton *triton, const inference::ModelMetadataResponse& model_metadata,
    const inference::ModelConfigResponse& model_config, model_info* info)
{
  if (model_metadata.inputs().size() != 1) {
    std::cerr << "expecting 1 input, got " << model_metadata.inputs().size()
              << std::endl;
    exit(1);
  }

  if (model_config.config().input().size() != 1) {
  std::cerr << "expecting 1 input in model configuration, got "
            << model_config.config().input().size() << std::endl;
    exit(1);
  }

  auto input_metadata = model_metadata.inputs(0);
  info->input_names.push_back(input_metadata.name());

  if (input_metadata.datatype().compare("UINT8") != 0 &&
    input_metadata.datatype().compare("INT8") != 0) {
    std::cerr << "expecting input datatype to be UINT8/INT8, model '"
              << model_metadata.name() << "' output type is '"
              << input_metadata.datatype() << "'" << std::endl;
    exit(1);
  } else {
    info->input_datatype = input_metadata.datatype();
  }

  for(int i=0; i < model_metadata.outputs().size(); i++)
  {
    size_t output_size = 4;
    auto output_metadata = model_metadata.outputs(i);
    info->output_names.push_back(output_metadata.name());
    for (int j=0; j < output_metadata.shape().size(); j++) {
      if (j == 0 && output_metadata.shape(j) <= 0) {
        output_size *= triton->batch_size;
      } else {
        output_size *= output_metadata.shape(j);
      }
    }
    info->output_sizes.push_back(output_size);
  }

  for(int n=0; n<input_metadata.shape().size(); n++)
  {
    info->input_shape.push_back(input_metadata.shape(n));
  }
}

void
get_model_info (GstElement *element)
{
  GstTriton *triton = GST_TRITON (element);
  TritonClient *triton_client = (TritonClient *)triton->client;

  tc::Error err;
  model_info *info = new model_info;
  char *model_name = triton->model_name;
  char *model_version = triton->model_version;
  unsigned int block_size = 0;
  unsigned int buf_size = 0;
  tc::Headers http_headers;

  if (triton->infer_mode == HTTP_MODE) {
    std::string model_metadata;
    err = triton_client->http_client_->ModelMetadata(
        &model_metadata, model_name, model_version, http_headers);
    if (!err.IsOk()) {
      std::cerr << "error: failed to get model metadata: " << err << std::endl;
    }
    rapidjson::Document model_metadata_json;
    err = parse_json(&model_metadata_json, model_metadata);
    if (!err.IsOk()) {
      std::cerr << "error: failed to parse model metadata: " << err
                << std::endl;
    }
    std::string model_config;
    err = triton_client->http_client_->ModelConfig(
        &model_config, model_name, model_version, http_headers);
    if (!err.IsOk()) {
      std::cerr << "error: failed to get model config: " << err << std::endl;
    }
    rapidjson::Document model_config_json;
    err = parse_json(&model_config_json, model_config);
    if (!err.IsOk()) {
      std::cerr << "error: failed to parse model config: " << err << std::endl;
    }
      parse_model_http(model_metadata_json, model_config_json, info);
  } else if (triton->infer_mode == GRPC_MODE) {
    inference::ModelMetadataResponse model_metadata;
    err = triton_client->grpc_client_->ModelMetadata(
        &model_metadata, model_name, model_version, http_headers);
    if (!err.IsOk()) {
      std::cerr << "error: failed to get model metadata: " << err << std::endl;
    }
    inference::ModelConfigResponse model_config;
    err = triton_client->grpc_client_->ModelConfig(
        &model_config, model_name, model_version, http_headers);
    if (!err.IsOk()) {
      std::cerr << "error: failed to get model config: " << err << std::endl;
    }
    parse_model_grpc(triton, model_metadata, model_config, info);
  } else {
    std::cerr << "error: failed to parse " << triton->infer_mode
              << "mode model config: " << std::endl;
  }
  if (triton->labels != NULL)
    gst_load_labels (triton->labels, info->labels);

  if (info->input_shape[BATCH_SIZE] <= 0) {
    if (triton->batch_size > 0) {
      info->input_shape[BATCH_SIZE] = triton->batch_size;
    } else {
      std::cerr << "error: Input batch size less than 0. "
        << "Please set batch size with 'batch-size' paramerter " << std::endl;
    }
  }

  block_size = info->input_shape[HEIGHT] * info->input_shape[WIDTH]
               * info->input_shape[CHANNEL];
  buf_size = info->input_shape[BATCH_SIZE] * block_size;
  triton->model_info = (void *)info;
  triton->input_buf = new(InputBuf);
  triton->block_size = block_size;
  triton->input_buf->size = buf_size;
  // init output buffer
  for (size_t i = 0; i < info->output_names.size(); i++) {
    if (info->output_names[i].find("results") != std::string::npos)
      triton->outputs = g_list_append (triton->outputs, g_new0 (result_info, 1));
  }

  if (triton->shm_key == NULL) {
    triton->input_buf->buf = (void *)calloc(1, buf_size);
    create_input (triton);
  } else {
    register_share_memory (triton);
  }
}

void
create_client_and_inferio (GstElement * element, gchar *url)
{
  GstTriton *triton = GST_TRITON (element);
  TritonClient *triton_client = new TritonClient;
  triton->infer_inputs = (gpointer) new std::vector<tc::InferInput*>;

  if (triton->infer_mode == HTTP_MODE) {
    tc::InferenceServerHttpClient::Create(&triton_client->http_client_, url, false);
  } else if (triton->infer_mode == GRPC_MODE) {
    tc::InferenceServerGrpcClient::Create(&triton_client->grpc_client_, url, false);
  } else {
    std::cerr << "error: Fail to crate triton client with "
              << triton->infer_mode << " mode" << std::endl;
  }
  triton->client = (gpointer) triton_client;
}

gint
parse_opencv_dtype (std::string dtype)
{
  if (dtype.compare("UINT8") == 0) {
    return CV_8UC3;
  } else if (dtype.compare("INT8") == 0) {
    return CV_8SC3;
  }
  return 0;
}

void
frame_to_inputbuf (GstMapInfo *mapinfo, GstObject * parent, guint idx)
{
  GstTriton *triton = GST_TRITON (parent);
  model_info *info = (model_info *) triton->model_info;
  gint lr_pad, tb_pad, img_width, img_height;
  gint vheight = triton->src_height;
  gint vwidth = triton->src_width;
  gint input_width = info->input_shape[WIDTH];
  gint input_height = info->input_shape[HEIGHT];
  gint dtype = parse_opencv_dtype (info->input_datatype);
  uint8_t *block_data = (uint8_t *)(triton->input_buf->buf) + triton->block_size * idx;

  cv::Mat input_mat(vheight, vwidth, CV_8UC3, mapinfo->data);
  cv::Mat output_mat(input_height, input_width, dtype, block_data);
  if (triton->keep_ratio) {
    calcuate_border(vwidth, vheight, input_width, input_height,
                   &lr_pad, &tb_pad, &img_width, &img_height);
    cv::Mat resized_mat;
    if (vwidth == img_width && vheight == img_height) {
      resized_mat = input_mat;
    } else {
      cv::resize(input_mat, resized_mat, cv::Size(img_width, img_height));
    }
    if (info->input_datatype.compare("UINT8") == 0) {
      cv::copyMakeBorder(resized_mat, output_mat, tb_pad, tb_pad, lr_pad, lr_pad,
                         cv::BORDER_CONSTANT, cv::Scalar(0,0,0));
    } else if (info->input_datatype.compare("INT8") == 0) {
      cv::Mat bordered_mat;
      cv::copyMakeBorder(resized_mat, bordered_mat, tb_pad, tb_pad, lr_pad, lr_pad,
                         cv::BORDER_CONSTANT, cv::Scalar(0,0,0));
      bordered_mat.convertTo(output_mat, dtype, 1, -128);
    }
  } else {
    if (info->input_datatype.compare("UINT8") == 0) {
      cv::resize(input_mat, output_mat, cv::Size(input_width, input_height));
    } else if (info->input_datatype.compare("INT8") == 0) {
      cv::Mat resized_mat;
      if (vwidth == input_width && vheight == input_height) {
        resized_mat = input_mat;
      } else {
        cv::resize(input_mat, resized_mat, cv::Size(input_width, input_height));
      }
      resized_mat.convertTo(output_mat, dtype, 1, -128);
    }
  }
}

gboolean
triton_infer (GstObject *parent, GstTritonRequest *request)
{
  GstTriton *triton = GST_TRITON (parent);
  model_info *info = (model_info *) triton->model_info;
  std::string datatype = info->input_datatype;
  tc::Error err;
  TritonClient *triton_client = (TritonClient *)triton->client;
  std::vector<tc::InferInput*> *inputs = (std::vector<tc::InferInput*> *) triton->infer_inputs;

  tc::InferOptions options(triton->model_name);
  options.model_version_ = triton->model_version;
  options.request_id_  = std::to_string(triton->infer_count);

  if (triton->infer_mode == HTTP_MODE) {
    triton_client->http_client_->AsyncInfer(
      [request](tc::InferResult* result) {
        {
          if (result->RequestStatus().IsOk()) {
            request->result = (void *)result;
            request->done = TRUE;
          } else {
            std::cerr << "error: Inference failed: "
                      << result->RequestStatus() << std::endl;
            exit(1);
          }
        }
      }, options, *inputs);
    usleep(10000);
  } else if (triton->infer_mode == GRPC_MODE) {
    triton_client->grpc_client_->AsyncInfer(
      [request](tc::InferResult* result) {
        {
          if (result->RequestStatus().IsOk()) {
            request->result = (void *)result;
            request->done = TRUE;
          } else {
            std::cerr << "error: Inference failed: "
                      << result->RequestStatus() << std::endl;
            exit(1);
          }
        }
      }, options, *inputs);
  }
  return TRUE;
}

void
bbox_check_value (float* point, float max)
{
  if (*point < 0)
    *point = 0;
  if (*point > max)
    *point = max;
}

void
bbox_mapping (float *detect_results, int in_num, int src_w, int src_h, int input_w,
     int input_h, int image_w, int image_h, int lr_pad, int tb_pad, bool keep_ratio)
{
  float *top, *left, *buttom, *right;
  for(int i=0; i < in_num; i++)
  {
    top = &detect_results[DETECT_RESULT_SIZE * i + TOP];
    left = &detect_results[DETECT_RESULT_SIZE * i + LEFT];
    buttom = &detect_results[DETECT_RESULT_SIZE * i + BOTTOM];
    right = &detect_results[DETECT_RESULT_SIZE * i + RIGHT];

    if (keep_ratio) {
      *top = (*top - tb_pad) / image_h * src_h;
      *left = (*left - lr_pad) / image_w * src_w;
      *buttom = (*buttom - tb_pad) / image_h * src_h;
      *right = (*right - lr_pad) / image_w * src_w;
    } else {
      *top = round(*top / input_h * src_h);
      *left = round(*left / input_w * src_w);
      *buttom = round(*buttom / input_h * src_h);
      *right = round(*right / input_w * src_w);
    }

    bbox_check_value (top, src_h);
    bbox_check_value (left, src_w);
    bbox_check_value (buttom, src_h);
    bbox_check_value (right, src_w);
  }
}

void
fill_to_result_info (GstTriton *triton, std::string output_name,
  result_info *info, float *output, size_t byte_size)
{
  if (output_name.find("results") != std::string::npos)
  {
    if (output_name.find("detection") != std::string::npos)
    {
      model_info * modelinfo = (model_info *)triton->model_info;
      bool keep_ratio = triton->keep_ratio;
      int src_w, src_h, input_w, input_h, lr_pad, tb_pad, img_w, img_h, detect_num;
      src_w = triton->src_width;
      src_h = triton->src_height;
      input_w = modelinfo->input_shape[WIDTH];
      input_h = modelinfo->input_shape[HEIGHT];
      detect_num = byte_size / (DETECT_RESULT_SIZE * sizeof(float));
      calcuate_border(src_w, src_h, input_w, input_h,
        &lr_pad, &tb_pad, &img_w, &img_h);
      bbox_mapping(output, detect_num, src_w, src_h, input_w, input_h,
        img_w, img_h, lr_pad, tb_pad, keep_ratio);
    }
    info->output = output;
    info->output_bytesize = byte_size;
  } else if (output_name.find("counts") != std::string::npos) {
    info->num = output;
    info->num_bytesize = byte_size;
  }
}

void
triton_parse_output (GstTriton *triton)
{
  tc::InferResult *result = (tc::InferResult *) triton->triton_result;
  result_info *result_temp;
  GList* outputs_temp_list = triton->outputs;
  model_info * info = (model_info *) triton->model_info;
  std::vector<std::string> output_names = info->output_names;
  std::vector<int64_t> output_sizes = info->output_sizes;
  float *output;
  size_t byte_size;
  for (size_t i = 0; i < output_names.size(); i++)
  {
    result_temp = (result_info *)outputs_temp_list->data;
    result->RawData (output_names[i], (const uint8_t **)&output, &byte_size);
    fill_to_result_info (triton, output_names[i], result_temp, output, byte_size);
    if (++i < output_names.size())
    {
      result->RawData (output_names[i], (const uint8_t **)&output, &byte_size);
      fill_to_result_info (triton, output_names[i], result_temp, output, byte_size);
    } else {
      break;
    }
    if (outputs_temp_list->next != NULL)
    {
      outputs_temp_list = outputs_temp_list->next;
    }
  }
}

void
draw_result (GstTriton *triton, GstMapInfo *mapinfo, guint channel)
{
  tc::InferResult *result = (tc::InferResult *) triton->triton_result;
  GList* outputs = triton->outputs;
  gint vheight = triton->src_height;
  gint vwidth = triton->src_width;
  tc::Error err;
  cv::Mat input_mat(vheight, vwidth, CV_8UC3, mapinfo->data);
  err = cv_mark(triton, result, outputs, input_mat, vwidth, vheight, channel);
  if (!err.IsOk()) {
    std::cerr << "The result is invalid. " << err
      << std::endl;
    exit(1);
  }
}

void delete_result (void *result)
{
  delete (tc::InferResult* )result;
  result = NULL;
}

void
free_inputbuf (GstTriton *triton)
{
  if (triton->shm_key == NULL) {
    free (triton->input_buf->buf);
  } else {
    unregister_share_memory (triton);
  }
}
