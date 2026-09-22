// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0.

#ifndef V4L2_CAMERA__YUYV_TO_BGR_HPP_
#define V4L2_CAMERA__YUYV_TO_BGR_HPP_

#include <cuda_runtime.h>

#include <cstddef>
#include <cstdint>

#include "v4l2_camera/v4l2_capture.hpp"

namespace v4l2_camera
{

class YuyvToBgrCuda
{
public:
  YuyvToBgrCuda(std::uint32_t width, std::uint32_t height, std::uint32_t bytes_per_line);
  ~YuyvToBgrCuda();

  YuyvToBgrCuda(const YuyvToBgrCuda &) = delete;
  YuyvToBgrCuda & operator=(const YuyvToBgrCuda &) = delete;

  void convert(const CapturedFrame & frame, std::uint8_t * output, cudaStream_t stream);

private:
  std::uint32_t width_;
  std::uint32_t height_;
  std::uint32_t bytes_per_line_;
  std::size_t input_bytes_;
  std::uint8_t * device_input_{nullptr};
};

}  // namespace v4l2_camera

#endif  // V4L2_CAMERA__YUYV_TO_BGR_HPP_
