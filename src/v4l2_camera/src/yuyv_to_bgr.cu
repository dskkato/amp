// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0.

#include "v4l2_camera/yuyv_to_bgr.hpp"

#include <linux/videodev2.h>

#include <limits>
#include <stdexcept>

#include "cuda_buffer/cuda_error.hpp"

namespace v4l2_camera
{
namespace
{

__device__ std::uint8_t clamp_u8(int value)
{
  return static_cast<std::uint8_t>(value < 0 ? 0 : (value > 255 ? 255 : value));
}

__global__ void yuyv_to_bgr_kernel(
  const std::uint8_t * input, std::uint8_t * output,
  std::uint32_t width, std::uint32_t height, std::uint32_t bytes_per_line)
{
  const std::uint32_t x = blockIdx.x * blockDim.x + threadIdx.x;
  const std::uint32_t y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= width || y >= height) {
    return;
  }

  const auto * pair = input + static_cast<std::size_t>(y) * bytes_per_line + (x / 2u) * 4u;
  const int luminance = x % 2u == 0u ? pair[0] : pair[2];
  const int u = static_cast<int>(pair[1]) - 128;
  const int v = static_cast<int>(pair[3]) - 128;
  const int c = luminance - 16;
  const int red = (298 * c + 409 * v + 128) >> 8;
  const int green = (298 * c - 100 * u - 208 * v + 128) >> 8;
  const int blue = (298 * c + 516 * u + 128) >> 8;

  auto * pixel = output + (static_cast<std::size_t>(y) * width + x) * 3u;
  pixel[0] = clamp_u8(blue);
  pixel[1] = clamp_u8(green);
  pixel[2] = clamp_u8(red);
}

}  // namespace

YuyvToBgrCuda::YuyvToBgrCuda(
  std::uint32_t width, std::uint32_t height, std::uint32_t bytes_per_line)
: width_(width), height_(height), bytes_per_line_(bytes_per_line),
  input_bytes_(static_cast<std::size_t>(bytes_per_line) * height)
{
  if (width == 0 || height == 0 || bytes_per_line < width * 2u || input_bytes_ == 0) {
    throw std::invalid_argument("invalid YUYV dimensions for CUDA converter");
  }
  CUDA_CHECK(cudaMalloc(reinterpret_cast<void **>(&device_input_), input_bytes_));
}

YuyvToBgrCuda::~YuyvToBgrCuda()
{
  if (device_input_ != nullptr) {
    (void)cudaFree(device_input_);
  }
}

void YuyvToBgrCuda::convert(
  const CapturedFrame & frame, std::uint8_t * output, cudaStream_t stream)
{
  if (frame.pixel_format != V4L2_PIX_FMT_YUYV || frame.width != width_ ||
    frame.height != height_ || frame.bytes_per_line != bytes_per_line_ ||
    frame.bytes_used < input_bytes_ || output == nullptr || stream == nullptr)
  {
    throw std::runtime_error("captured frame is incompatible with CUDA YUYV converter");
  }

  CUDA_CHECK(cudaMemcpyAsync(
    device_input_, frame.data, input_bytes_, cudaMemcpyHostToDevice, stream));
  const dim3 block(16, 16);
  const dim3 grid((width_ + block.x - 1u) / block.x, (height_ + block.y - 1u) / block.y);
  yuyv_to_bgr_kernel<<<grid, block, 0, stream>>>(
    device_input_, output, width_, height_, bytes_per_line_);
  CUDA_CHECK(cudaGetLastError());
}

}  // namespace v4l2_camera
