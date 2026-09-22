// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "v4l2_camera/v4l2_capture.hpp"

#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>

namespace v4l2_camera
{
namespace
{

int xioctl(int fd, unsigned long request, void * argument)
{
  int result;
  do {
    result = ::ioctl(fd, request, argument);
  } while (result == -1 && errno == EINTR);
  return result;
}

std::string errno_message(const std::string & operation)
{
  std::ostringstream stream;
  stream << operation << ": " << std::strerror(errno);
  return stream.str();
}

std::uint32_t requested_pixel_format(const std::string & format)
{
  if (format == "yuyv" || format == "YUYV") {
    return V4L2_PIX_FMT_YUYV;
  }
  if (format == "mjpeg" || format == "MJPEG" || format == "mjpg") {
    return V4L2_PIX_FMT_MJPEG;
  }
  throw std::invalid_argument(
    "pixel_format must be auto, yuyv, or mjpeg (got '" + format + "')");
}

}  // namespace

V4L2Capture::~V4L2Capture()
{
  close();
}

void V4L2Capture::open(
  const std::string & device,
  std::uint32_t requested_width,
  std::uint32_t requested_height,
  double requested_fps,
  const std::string & requested_format)
{
  close();

  if (requested_width == 0 || requested_height == 0) {
    throw std::invalid_argument("image_width and image_height must be greater than zero");
  }
  if (!(requested_fps > 0.0) || !std::isfinite(requested_fps)) {
    throw std::invalid_argument("frame_rate must be finite and greater than zero");
  }

  device_ = device;
  fd_ = ::open(device.c_str(), O_RDWR | O_NONBLOCK);
  if (fd_ < 0) {
    throw std::runtime_error(errno_message("open(" + device + ")"));
  }

  try {
    v4l2_capability capability{};
    if (xioctl(fd_, VIDIOC_QUERYCAP, &capability) < 0) {
      throw std::runtime_error(errno_message("VIDIOC_QUERYCAP"));
    }
    if ((capability.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
      throw std::runtime_error(device + " is not a single-planar video capture device");
    }
    if ((capability.capabilities & V4L2_CAP_STREAMING) == 0) {
      throw std::runtime_error(device + " does not support V4L2 streaming capture");
    }

    std::vector<std::uint32_t> formats;
    if (requested_format == "auto") {
      formats = {V4L2_PIX_FMT_MJPEG, V4L2_PIX_FMT_YUYV};
    } else {
      formats = {requested_pixel_format(requested_format)};
    }

    bool format_selected = false;
    for (const auto format : formats) {
      v4l2_format video_format{};
      video_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      video_format.fmt.pix.width = requested_width;
      video_format.fmt.pix.height = requested_height;
      video_format.fmt.pix.pixelformat = format;
      video_format.fmt.pix.field = V4L2_FIELD_ANY;

      if (xioctl(fd_, VIDIOC_S_FMT, &video_format) == 0 &&
        video_format.fmt.pix.pixelformat == format &&
        video_format.fmt.pix.width > 0 && video_format.fmt.pix.height > 0)
      {
        width_ = video_format.fmt.pix.width;
        height_ = video_format.fmt.pix.height;
        pixel_format_ = video_format.fmt.pix.pixelformat;
        bytes_per_line_ = video_format.fmt.pix.bytesperline;
        if (bytes_per_line_ == 0) {
          bytes_per_line_ =
            pixel_format_ == V4L2_PIX_FMT_YUYV ? width_ * 2u : width_ * 3u;
        }
        format_selected = true;
        break;
      }
    }
    if (!format_selected) {
      throw std::runtime_error(
        "could not negotiate YUYV or MJPEG on " + device +
        "; inspect supported formats with v4l2-ctl --list-formats-ext");
    }

    v4l2_streamparm stream_parameters{};
    stream_parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_G_PARM, &stream_parameters) == 0 &&
      (stream_parameters.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) != 0)
    {
      const auto denominator = static_cast<std::uint32_t>(
        std::max(1.0, std::round(requested_fps)));
      stream_parameters.parm.capture.timeperframe.numerator = 1;
      stream_parameters.parm.capture.timeperframe.denominator = denominator;
      // Drivers are allowed to adjust the requested interval.  A failure here
      // is non-fatal because some UVC devices accept the format but reject a
      // specific interval.
      (void)xioctl(fd_, VIDIOC_S_PARM, &stream_parameters);
    }

    v4l2_requestbuffers request{};
    request.count = 4;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_REQBUFS, &request) < 0) {
      throw std::runtime_error(errno_message("VIDIOC_REQBUFS"));
    }
    if (request.count < 2) {
      throw std::runtime_error("V4L2 returned fewer than two capture buffers");
    }

    buffers_.resize(request.count);
    for (std::uint32_t index = 0; index < request.count; ++index) {
      v4l2_buffer buffer{};
      buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buffer.memory = V4L2_MEMORY_MMAP;
      buffer.index = index;
      if (xioctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0) {
        throw std::runtime_error(errno_message("VIDIOC_QUERYBUF"));
      }

      void * address = ::mmap(
        nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buffer.m.offset);
      if (address == MAP_FAILED) {
        throw std::runtime_error(errno_message("mmap"));
      }
      buffers_[index] = MappedBuffer{address, buffer.length};
    }

    for (std::uint32_t index = 0; index < buffers_.size(); ++index) {
      v4l2_buffer buffer{};
      buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buffer.memory = V4L2_MEMORY_MMAP;
      buffer.index = index;
      if (xioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
        throw std::runtime_error(errno_message("VIDIOC_QBUF"));
      }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
      throw std::runtime_error(errno_message("VIDIOC_STREAMON"));
    }
  } catch (...) {
    close();
    throw;
  }
}

bool V4L2Capture::dequeue(CapturedFrame & frame, int timeout_ms)
{
  if (fd_ < 0) {
    throw std::runtime_error("V4L2 capture is not open");
  }

  pollfd descriptor{};
  descriptor.fd = fd_;
  descriptor.events = POLLIN;
  int result;
  do {
    result = ::poll(&descriptor, 1, timeout_ms);
  } while (result < 0 && errno == EINTR);

  if (result == 0) {
    return false;
  }
  if (result < 0) {
    throw std::runtime_error(errno_message("poll"));
  }
  if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
    throw std::runtime_error("V4L2 device reported a poll error");
  }

  v4l2_buffer buffer{};
  buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buffer.memory = V4L2_MEMORY_MMAP;
  if (xioctl(fd_, VIDIOC_DQBUF, &buffer) < 0) {
    if (errno == EAGAIN) {
      return false;
    }
    throw std::runtime_error(errno_message("VIDIOC_DQBUF"));
  }
  if (buffer.index >= buffers_.size()) {
    throw std::runtime_error("V4L2 returned an invalid buffer index");
  }

  frame.data = static_cast<const std::uint8_t *>(buffers_[buffer.index].address);
  frame.bytes_used = std::min<std::size_t>(buffer.bytesused, buffers_[buffer.index].length);
  frame.buffer_length = buffers_[buffer.index].length;
  frame.pixel_format = pixel_format_;
  frame.width = width_;
  frame.height = height_;
  frame.bytes_per_line = bytes_per_line_;
  frame.index = buffer.index;
  return true;
}

void V4L2Capture::queue(const CapturedFrame & frame)
{
  if (fd_ < 0) {
    return;
  }
  v4l2_buffer buffer{};
  buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buffer.memory = V4L2_MEMORY_MMAP;
  buffer.index = frame.index;
  if (xioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
    throw std::runtime_error(errno_message("VIDIOC_QBUF"));
  }
}

void V4L2Capture::close() noexcept
{
  if (fd_ >= 0) {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    (void)xioctl(fd_, VIDIOC_STREAMOFF, &type);
  }

  for (const auto & buffer : buffers_) {
    if (buffer.address != nullptr && buffer.address != MAP_FAILED) {
      (void)::munmap(buffer.address, buffer.length);
    }
  }
  buffers_.clear();

  if (fd_ >= 0) {
    (void)::close(fd_);
  }
  fd_ = -1;
  width_ = 0;
  height_ = 0;
  pixel_format_ = 0;
  bytes_per_line_ = 0;
}

}  // namespace v4l2_camera
