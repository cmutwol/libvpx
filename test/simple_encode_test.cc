/*
 *  Copyright (c) 2019 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <math.h>
#include <memory>
#include <vector>
#include "third_party/googletest/src/include/gtest/gtest.h"
#include "vp9/simple_encode.h"

namespace vp9 {
namespace {

// TODO(angirbid): Find a better way to construct encode info
const int w = 352;
const int h = 288;
const int frame_rate_num = 30;
const int frame_rate_den = 1;
const int target_bitrate = 1000;
const int num_frames = 17;
const char infile_path[] = "bus_352x288_420_f20_b8.yuv";

double GetBitrateInKbps(size_t bit_size, int num_frames, int frame_rate_num,
                        int frame_rate_den) {
  return static_cast<double>(bit_size) / num_frames * frame_rate_num /
         frame_rate_den / 1000.0;
}

// Returns the number of unit in size of 4.
// For example, if size is 7, return 2.
int GetNumUnit4x4(int size) { return (size + 3) >> 2; }

TEST(SimpleEncode, ComputeFirstPassStats) {
  SimpleEncode simple_encode(w, h, frame_rate_num, frame_rate_den,
                             target_bitrate, num_frames, infile_path);
  simple_encode.ComputeFirstPassStats();
  std::vector<std::vector<double>> frame_stats =
      simple_encode.ObserveFirstPassStats();
  EXPECT_EQ(frame_stats.size(), static_cast<size_t>(num_frames));
  size_t data_num = frame_stats[0].size();
  // Read ObserveFirstPassStats before changing FIRSTPASS_STATS.
  EXPECT_EQ(data_num, static_cast<size_t>(25));
  for (size_t i = 0; i < frame_stats.size(); ++i) {
    EXPECT_EQ(frame_stats[i].size(), data_num);
    // FIRSTPASS_STATS's first element is frame
    EXPECT_EQ(frame_stats[i][0], i);
    // FIRSTPASS_STATS's last element is count, and the count is 1 for single
    // frame stats
    EXPECT_EQ(frame_stats[i][data_num - 1], 1);
  }
}

TEST(SimpleEncode, GetCodingFrameNum) {
  SimpleEncode simple_encode(w, h, frame_rate_num, frame_rate_den,
                             target_bitrate, num_frames, infile_path);
  simple_encode.ComputeFirstPassStats();
  int num_coding_frames = simple_encode.GetCodingFrameNum();
  EXPECT_EQ(num_coding_frames, 19);
}

TEST(SimpleEncode, EncodeFrame) {
  SimpleEncode simple_encode(w, h, frame_rate_num, frame_rate_den,
                             target_bitrate, num_frames, infile_path);
  simple_encode.ComputeFirstPassStats();
  int num_coding_frames = simple_encode.GetCodingFrameNum();
  EXPECT_GE(num_coding_frames, num_frames);
  // The coding frames include actual show frames and alternate reference
  // frames, i.e. no show frame.
  int ref_num_alternate_refereces = num_coding_frames - num_frames;
  int num_alternate_refereces = 0;
  simple_encode.StartEncode();
  size_t total_data_bit_size = 0;
  for (int i = 0; i < num_coding_frames; ++i) {
    EncodeFrameResult encode_frame_result;
    simple_encode.EncodeFrame(&encode_frame_result);
    if (i == 0) {
      EXPECT_EQ(encode_frame_result.show_idx, 0);
      EXPECT_EQ(encode_frame_result.frame_type, kKeyFrame)
          << "The first coding frame should be key frame";
    }
    if (encode_frame_result.frame_type == kAlternateReference) {
      ++num_alternate_refereces;
    }
    EXPECT_GE(encode_frame_result.show_idx, 0);
    EXPECT_LT(encode_frame_result.show_idx, num_frames);
    if (i == num_coding_frames - 1) {
      EXPECT_EQ(encode_frame_result.show_idx, num_frames - 1)
          << "The last coding frame should be the last display order";
    }
    EXPECT_GE(encode_frame_result.psnr, 34)
        << "The psnr is supposed to be greater than 34 given the "
           "target_bitrate 1000 kbps";
    total_data_bit_size += encode_frame_result.coding_data_bit_size;
  }
  EXPECT_EQ(num_alternate_refereces, ref_num_alternate_refereces);
  const double bitrate = GetBitrateInKbps(total_data_bit_size, num_frames,
                                          frame_rate_num, frame_rate_den);
  const double off_target_threshold = 150;
  EXPECT_LE(fabs(target_bitrate - bitrate), off_target_threshold);
  simple_encode.EndEncode();
}

TEST(SimpleEncode, EncodeFrameWithQuantizeIndex) {
  SimpleEncode simple_encode(w, h, frame_rate_num, frame_rate_den,
                             target_bitrate, num_frames, infile_path);
  simple_encode.ComputeFirstPassStats();
  int num_coding_frames = simple_encode.GetCodingFrameNum();
  simple_encode.StartEncode();
  for (int i = 0; i < num_coding_frames; ++i) {
    const int assigned_quantize_index = 100 + i;
    EncodeFrameResult encode_frame_result;
    simple_encode.EncodeFrameWithQuantizeIndex(&encode_frame_result,
                                               assigned_quantize_index);
    EXPECT_EQ(encode_frame_result.quantize_index, assigned_quantize_index);
  }
  simple_encode.EndEncode();
}

TEST(SimpleEncode, EncodeConsistencyTest) {
  std::vector<int> quantize_index_list;
  std::vector<uint64_t> ref_sse_list;
  std::vector<double> ref_psnr_list;
  std::vector<size_t> ref_bit_size_list;
  {
    // The first encode.
    SimpleEncode simple_encode(w, h, frame_rate_num, frame_rate_den,
                               target_bitrate, num_frames, infile_path);
    simple_encode.ComputeFirstPassStats();
    const int num_coding_frames = simple_encode.GetCodingFrameNum();
    simple_encode.StartEncode();
    for (int i = 0; i < num_coding_frames; ++i) {
      EncodeFrameResult encode_frame_result;
      simple_encode.EncodeFrame(&encode_frame_result);
      quantize_index_list.push_back(encode_frame_result.quantize_index);
      ref_sse_list.push_back(encode_frame_result.sse);
      ref_psnr_list.push_back(encode_frame_result.psnr);
      ref_bit_size_list.push_back(encode_frame_result.coding_data_bit_size);
    }
    simple_encode.EndEncode();
  }
  {
    // The second encode with quantize index got from the first encode.
    SimpleEncode simple_encode(w, h, frame_rate_num, frame_rate_den,
                               target_bitrate, num_frames, infile_path);
    simple_encode.ComputeFirstPassStats();
    const int num_coding_frames = simple_encode.GetCodingFrameNum();
    EXPECT_EQ(static_cast<size_t>(num_coding_frames),
              quantize_index_list.size());
    simple_encode.StartEncode();
    for (int i = 0; i < num_coding_frames; ++i) {
      EncodeFrameResult encode_frame_result;
      simple_encode.EncodeFrameWithQuantizeIndex(&encode_frame_result,
                                                 quantize_index_list[i]);
      EXPECT_EQ(encode_frame_result.quantize_index, quantize_index_list[i]);
      EXPECT_EQ(encode_frame_result.sse, ref_sse_list[i]);
      EXPECT_DOUBLE_EQ(encode_frame_result.psnr, ref_psnr_list[i]);
      EXPECT_EQ(encode_frame_result.coding_data_bit_size, ref_bit_size_list[i]);
    }
    simple_encode.EndEncode();
  }
}

// Test the information stored in encoder is the same between two encode runs.
TEST(SimpleEncode, EncodeConsistencyTest2) {
  const int num_rows_4x4 = GetNumUnit4x4(w);
  const int num_cols_4x4 = GetNumUnit4x4(h);
  const int num_units_4x4 = num_rows_4x4 * num_cols_4x4;
  // The first encode.
  SimpleEncode simple_encode(w, h, frame_rate_num, frame_rate_den,
                             target_bitrate, num_frames, infile_path);
  simple_encode.ComputeFirstPassStats();
  const int num_coding_frames = simple_encode.GetCodingFrameNum();
  std::vector<PartitionInfo> partition_info_list(num_units_4x4 *
                                                 num_coding_frames);
  simple_encode.StartEncode();
  for (int i = 0; i < num_coding_frames; ++i) {
    EncodeFrameResult encode_frame_result;
    simple_encode.EncodeFrame(&encode_frame_result);
    for (int j = 0; j < num_rows_4x4 * num_cols_4x4; ++j) {
      partition_info_list[i * num_units_4x4 + j] =
          encode_frame_result.partition_info[j];
    }
  }
  simple_encode.EndEncode();
  // The second encode.
  SimpleEncode simple_encode_2(w, h, frame_rate_num, frame_rate_den,
                               target_bitrate, num_frames, infile_path);
  simple_encode_2.ComputeFirstPassStats();
  const int num_coding_frames_2 = simple_encode_2.GetCodingFrameNum();
  simple_encode_2.StartEncode();
  for (int i = 0; i < num_coding_frames_2; ++i) {
    EncodeFrameResult encode_frame_result;
    simple_encode_2.EncodeFrame(&encode_frame_result);
    for (int j = 0; j < num_rows_4x4 * num_cols_4x4; ++j) {
      EXPECT_EQ(encode_frame_result.partition_info[j].row,
                partition_info_list[i * num_units_4x4 + j].row);
      EXPECT_EQ(encode_frame_result.partition_info[j].column,
                partition_info_list[i * num_units_4x4 + j].column);
      EXPECT_EQ(encode_frame_result.partition_info[j].row_start,
                partition_info_list[i * num_units_4x4 + j].row_start);
      EXPECT_EQ(encode_frame_result.partition_info[j].column_start,
                partition_info_list[i * num_units_4x4 + j].column_start);
      EXPECT_EQ(encode_frame_result.partition_info[j].width,
                partition_info_list[i * num_units_4x4 + j].width);
      EXPECT_EQ(encode_frame_result.partition_info[j].height,
                partition_info_list[i * num_units_4x4 + j].height);
    }
  }
  simple_encode_2.EndEncode();
}

TEST(SimpleEncode, GetEncodeFrameInfo) {
  // Makes sure that the encode_frame_info obtained from GetEncodeFrameInfo()
  // matches the counterpart in encode_frame_result obtained from EncodeFrame()
  SimpleEncode simple_encode(w, h, frame_rate_num, frame_rate_den,
                             target_bitrate, num_frames, infile_path);
  simple_encode.ComputeFirstPassStats();
  const int num_coding_frames = simple_encode.GetCodingFrameNum();
  simple_encode.StartEncode();
  for (int i = 0; i < num_coding_frames; ++i) {
    EncodeFrameInfo encode_frame_info = simple_encode.GetNextEncodeFrameInfo();
    EncodeFrameResult encode_frame_result;
    simple_encode.EncodeFrame(&encode_frame_result);
    EXPECT_EQ(encode_frame_info.show_idx, encode_frame_result.show_idx);
    EXPECT_EQ(encode_frame_info.frame_type, encode_frame_result.frame_type);
  }
  simple_encode.EndEncode();
}

TEST(SimpleEncode, GetFramePixelCount) {
  SimpleEncode simple_encode(w, h, frame_rate_num, frame_rate_den,
                             target_bitrate, num_frames, infile_path);
  EXPECT_EQ(simple_encode.GetFramePixelCount(),
            static_cast<uint64_t>(w * h * 3 / 2));
}

}  // namespace
}  // namespace vp9
