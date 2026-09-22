// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#ifndef V4L2_CAMERA__V4L2_CAPTURE_HPP_
#define V4L2_CAMERA__V4L2_CAPTURE_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace v4l2_camera
{

struct CapturedFrame
{
  const std::uint8_t * data{nullptr};
  std::size_t bytes_used{0};
  std::size_t buffer_length{0};
  std::uint32_t pixel_format{0};
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::uint32_t bytes_per_line{0};
  std::uint32_t index{0};
};

class V4L2Capture
{
public:
  V4L2Capture() = default;
  ~V4L2Capture();

  V4L2Capture(const V4L2Capture &) = delete;
  V4L2Capture & operator=(const V4L2Capture &) = delete;

  void open(
    const std::string & device,
    std::uint32_t requested_width,
    std::uint32_t requested_height,
    double requested_fps,
    const std::string & requested_format);

  bool dequeue(CapturedFrame & frame, int timeout_ms);
  void queue(const CapturedFrame & frame);
  void close() noexcept;

  std::uint32_t width() const noexcept {return width_;}
  std::uint32_t height() const noexcept {return height_;}
  std::uint32_t pixel_format() const noexcept {return pixel_format_;}
  std::uint32_t bytes_per_line() const noexcept {return bytes_per_line_;}
  const std::string & device() const noexcept {return device_;}

private:
  struct MappedBuffer
  {
    void * address{nullptr};
    std::size_t length{0};
  };

  int fd_{-1};
  std::string device_;
  std::vector<MappedBuffer> buffers_;
  std::uint32_t width_{0};
  std::uint32_t height_{0};
  std::uint32_t pixel_format_{0};
  std::uint32_t bytes_per_line_{0};
};

}  // namespace v4l2_camera

#endif  // V4L2_CAMERA__V4L2_CAPTURE_HPP_
