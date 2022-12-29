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

#include <string>
#include <vector>
#include "label_mark.h"

#define MAX_CLS_NUM 5
typedef struct
{
  int64_t batchsize;
  float *output;
  size_t output_bytesize;
  float *num;
  size_t num_bytesize;
} result_info;

typedef struct
{
  std::string name;
  cv::Scalar color;
} label_info;

typedef struct
{
  std::vector<std::string> input_names;
  std::vector<std::string> output_names;
  std::vector<label_info> labels;
  std::vector<int64_t> input_shape;
} model_info;

enum
{
  BATCH_SIZE,
  HEIGHT,
  WIDTH,
  CHANNEL,
};

std::string float2string(float score)
{
  size_t start_pos = 0, select_pos = 0;
  std::ostringstream score_stream;
  std::string score_str;
  score_stream << score * 100;
  score_str = score_stream.str();
  select_pos = score_str.find(".");
  if (select_pos != std::string::npos && (select_pos + 3) <= score_str.size())
    score_str = score_str.substr(start_pos, select_pos + 3);
  return score_str;
}

int get_batch_result_offset(int32_t *detect_num, guint channel)
{
  int offset = 0;
  for (guint i = 0; i < channel; i++)
  {
    offset += detect_num[i];
  }
  return offset;
}

tc::Error cv_mark(GstTriton *triton, GstTritonRequest *request, cv::Mat &input_mat, gint src_w, gint src_h, guint channel)
{
  tc::Error err;
  model_info *info = (model_info *) triton->model_info;
  GstTritonTask task = triton->task;
  tc::InferResult *output = (tc::InferResult *)request->result;
  int input_w = info->input_shape[WIDTH];
  int input_h = info->input_shape[HEIGHT];
  double scale = 1.0;
  cv::Scalar color = cv::Scalar(255, 0, 0);
  int thickness = (src_w > 500) ? 2 : 1;

  result_info * result = (result_info *)request->outputs->data;
  if (output != NULL) {
    switch (task)
    {
      case DETECTION:
      {
        int32_t detect_num = ((int32_t *) result->num)[channel];
        float *detect_results = ((float *) result->output)
          + get_batch_result_offset((int32_t *) result->num, channel) * DETECT_RESULT_SIZE;
        float top, left, bottom, right, cls_id, score;
        float top_offset = 5;
        std::string label;
        for (int32_t i=0; i< detect_num; i++)
        {
          top = detect_results[DETECT_RESULT_SIZE * i + TOP];
          left = detect_results[DETECT_RESULT_SIZE * i + LEFT];
          bottom = detect_results[DETECT_RESULT_SIZE * i + BOTTOM];
          right = detect_results[DETECT_RESULT_SIZE * i + RIGHT];
          scale = (right - left) / 200;
          if (scale > 2)
            scale = 2;

          score = detect_results[DETECT_RESULT_SIZE * i + SCORE];
          cls_id = detect_results[DETECT_RESULT_SIZE * i + CLASSE];
          if (!info->labels.empty() && size_t(i) <= info->labels.size())
          {
            color = info->labels[cls_id + 1].color;
            label = info->labels[cls_id + 1].name
               + ": " + float2string(score) + "%";
          } else {
            label = "ClassID " + std::to_string(int(cls_id)) + ": "
              + float2string(score) + "%";
          }
          cv::rectangle(input_mat, cv::Point(left, top),
            cv::Point(right, bottom), color, thickness, cv::LINE_AA);
          if (top-top_offset >= 0)
            top -= top_offset;
          cv::putText(input_mat, label, cv::Point(left, top),
            cv::FONT_HERSHEY_SIMPLEX, scale, color, thickness, cv::LINE_AA);
        }
        return tc::Error::Success;
      }
      case CLASSIFICATION:
      {
        int32_t detect_num = ((int32_t *) result->num)[channel];
        detect_num = detect_num < MAX_CLS_NUM ? detect_num : MAX_CLS_NUM;
        float *cls_results = ((float *) result->output)
          + get_batch_result_offset((int32_t *) result->num, channel) * CLS_RESULT_SIZE;
        uint32_t text_top = 5, text_left = 5;
        uint32_t text_top_offset = src_h / 14;
        scale *= src_w / ((src_w > 500) ? 800.0 : 500.0);
        std::string label;

        for (int i=0; i < detect_num; i++)
        {
          if (!info->labels.empty() && size_t(i) <= info->labels.size())
          {
            color = info->labels[int(cls_results[i*CLS_RESULT_SIZE])+1].color;
            label = info->labels[int(cls_results[i*CLS_RESULT_SIZE])+1].name
              + ": " + float2string(cls_results[i*CLS_RESULT_SIZE+1]) + "%";
          } else {
            label = "ClassID " + std::to_string(int(cls_results[i*CLS_RESULT_SIZE])) + ": "
              + float2string(cls_results[i*CLS_RESULT_SIZE+1]) + "%";
          }
          cv::putText(input_mat, label, cv::Point(text_left, text_top + text_top_offset * (i + 1)),
            cv::FONT_HERSHEY_SIMPLEX, scale, color, thickness, cv::LINE_AA);
        }
        return tc::Error::Success;
      }
      case SEGMENTATION:
      {
        uint8_t *result_data = ((uint8_t *) (result->output))
          + (input_h * input_w * 3 * sizeof(uint8_t) * channel);
        cv::Mat result_mat(input_h, input_w, CV_8UC3, result_data);
        cv::Mat resized_mat;
        cv::resize(result_mat, resized_mat, cv::Size(src_w, src_h));
        cv::Mat dst;
        addWeighted(input_mat, 0.2, resized_mat, 0.8, 0.0, dst);
        dst.copyTo(input_mat);
        return tc::Error::Success;
      }
      default:
        return tc::Error("The task type is invalid. ");
    }
  } else {
    return tc::Error("The result is invalid. ");
  }
}
