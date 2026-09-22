// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "v4l2_camera/pixel_converter.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>
#include <linux/videodev2.h>

namespace v4l2_camera
{

TEST(PixelConverter, ConvertsBlackAndWhiteYuyvToBgr)
{
  const std::uint8_t yuyv[] = {16, 128, 235, 128};
  CapturedFrame frame;
  frame.data = yuyv;
  frame.bytes_used = sizeof(yuyv);
  frame.pixel_format = V4L2_PIX_FMT_YUYV;
  frame.width = 2;
  frame.height = 1;
  frame.bytes_per_line = sizeof(yuyv);

  std::vector<std::uint8_t> output(6, 0);
  convert_frame(frame, "bgr8", output.data(), output.size());

  EXPECT_EQ(output, (std::vector<std::uint8_t>{0, 0, 0, 255, 255, 255}));
}

TEST(PixelConverter, ConvertsYuyvLuminanceToMono)
{
  const std::uint8_t yuyv[] = {16, 128, 235, 128};
  CapturedFrame frame;
  frame.data = yuyv;
  frame.bytes_used = sizeof(yuyv);
  frame.pixel_format = V4L2_PIX_FMT_YUYV;
  frame.width = 2;
  frame.height = 1;
  frame.bytes_per_line = sizeof(yuyv);

  std::vector<std::uint8_t> output(2, 0);
  convert_frame(frame, "mono8", output.data(), output.size());

  EXPECT_EQ(output, (std::vector<std::uint8_t>{16, 235}));
}

TEST(PixelConverter, RejectsUnknownOutputEncoding)
{
  EXPECT_THROW(output_bytes_per_pixel("rgba8"), std::invalid_argument);
}

}  // namespace v4l2_camera
