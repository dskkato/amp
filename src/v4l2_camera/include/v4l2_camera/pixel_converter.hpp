// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#ifndef V4L2_CAMERA__PIXEL_CONVERTER_HPP_
#define V4L2_CAMERA__PIXEL_CONVERTER_HPP_

#include <cstddef>
#include <cstdint>
#include <string>

#include "v4l2_camera/v4l2_capture.hpp"

namespace v4l2_camera
{

/// Convert a captured V4L2 frame into a tightly packed ROS image payload.
///
/// Supported input formats are V4L2_PIX_FMT_YUYV and V4L2_PIX_FMT_MJPEG.
/// Supported output encodings are bgr8, rgb8, and mono8.
void convert_frame(
  const CapturedFrame & frame,
  const std::string & output_encoding,
  std::uint8_t * output,
  std::size_t output_size);

std::size_t output_bytes_per_pixel(const std::string & output_encoding);

}  // namespace v4l2_camera

#endif  // V4L2_CAMERA__PIXEL_CONVERTER_HPP_
