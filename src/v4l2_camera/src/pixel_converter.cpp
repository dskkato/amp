// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "v4l2_camera/pixel_converter.hpp"

#include <csetjmp>
#include <cstdlib>
#include <cstring>
#include <jpeglib.h>
#include <linux/videodev2.h>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

namespace v4l2_camera
{
namespace
{

std::uint8_t clamp_u8(int value)
{
  if (value < 0) {
    return 0;
  }
  if (value > 255) {
    return 255;
  }
  return static_cast<std::uint8_t>(value);
}

void check_output_buffer(
  const CapturedFrame & frame,
  const std::string & output_encoding,
  std::uint8_t * output,
  std::size_t output_size)
{
  if (output == nullptr) {
    throw std::invalid_argument("output buffer must not be null");
  }
  const auto bytes_per_pixel = output_bytes_per_pixel(output_encoding);
  if (frame.width == 0 || frame.height == 0 ||
    frame.width > std::numeric_limits<std::size_t>::max() / bytes_per_pixel ||
    static_cast<std::size_t>(frame.width) * frame.height >
    std::numeric_limits<std::size_t>::max() / bytes_per_pixel)
  {
    throw std::runtime_error("image dimensions overflow output buffer size");
  }
  const auto required = static_cast<std::size_t>(frame.width) * frame.height * bytes_per_pixel;
  if (output_size < required) {
    throw std::invalid_argument("output buffer is smaller than the converted image");
  }
}

void convert_yuyv(
  const CapturedFrame & frame,
  const std::string & output_encoding,
  std::uint8_t * output)
{
  const std::size_t bytes_per_pixel = output_bytes_per_pixel(output_encoding);
  const std::size_t minimum_row_bytes = static_cast<std::size_t>(frame.width) * 2u;
  if (frame.bytes_per_line < minimum_row_bytes ||
    static_cast<std::size_t>(frame.bytes_per_line) * frame.height > frame.bytes_used)
  {
    throw std::runtime_error("truncated YUYV frame");
  }

  for (std::uint32_t y = 0; y < frame.height; ++y) {
    const auto * input = frame.data + static_cast<std::size_t>(y) * frame.bytes_per_line;
    auto * row = output + static_cast<std::size_t>(y) * frame.width * bytes_per_pixel;
    for (std::uint32_t x = 0; x < frame.width; x += 2) {
      const int y0 = input[0];
      const int u = input[1] - 128;
      const int y1 = (x + 1 < frame.width) ? input[2] : input[0];
      const int v = (x + 1 < frame.width) ? input[3] - 128 : 0;

      if (output_encoding == "mono8") {
        row[0] = static_cast<std::uint8_t>(y0);
        if (x + 1 < frame.width) {
          row[1] = static_cast<std::uint8_t>(y1);
        }
      } else {
        const auto write_pixel = [&](std::uint8_t * destination, int luminance) {
            const int c = luminance - 16;
            const int red = (298 * c + 409 * v + 128) >> 8;
            const int green = (298 * c - 100 * u - 208 * v + 128) >> 8;
            const int blue = (298 * c + 516 * u + 128) >> 8;
            if (output_encoding == "rgb8") {
              destination[0] = clamp_u8(red);
              destination[1] = clamp_u8(green);
              destination[2] = clamp_u8(blue);
            } else {
              destination[0] = clamp_u8(blue);
              destination[1] = clamp_u8(green);
              destination[2] = clamp_u8(red);
            }
          };
        write_pixel(row, y0);
        if (x + 1 < frame.width) {
          write_pixel(row + 3, y1);
        }
      }
      input += 4;
      row += output_encoding == "mono8" ? 2 : 6;
    }
  }
}

struct JpegErrorManager
{
  jpeg_error_mgr public_manager{};
  jmp_buf jump_buffer{};
  char message[JMSG_LENGTH_MAX]{};
};

void jpeg_error_exit(j_common_ptr common)
{
  auto * error = reinterpret_cast<JpegErrorManager *>(common->err);
  (*common->err->format_message)(common, error->message);
  longjmp(error->jump_buffer, 1);
}

void convert_mjpeg(
  const CapturedFrame & frame,
  const std::string & output_encoding,
  std::uint8_t * output)
{
  jpeg_decompress_struct decompressor{};
  JpegErrorManager error{};
  decompressor.err = jpeg_std_error(&error.public_manager);
  error.public_manager.error_exit = jpeg_error_exit;

  bool created = false;
  std::uint8_t * row_data = nullptr;
  if (setjmp(error.jump_buffer) != 0) {
    if (created) {
      jpeg_destroy_decompress(&decompressor);
    }
    std::free(row_data);
    throw std::runtime_error(
            std::string("MJPEG decode failed: ") +
            (error.message[0] == '\0' ? "unknown JPEG error" : error.message));
  }

  jpeg_create_decompress(&decompressor);
  created = true;
  jpeg_mem_src(&decompressor, const_cast<unsigned char *>(frame.data), frame.bytes_used);
  jpeg_read_header(&decompressor, TRUE);
  decompressor.out_color_space = output_encoding == "mono8" ? JCS_GRAYSCALE : JCS_RGB;
  jpeg_start_decompress(&decompressor);

  if (decompressor.output_width != frame.width || decompressor.output_height != frame.height) {
    jpeg_destroy_decompress(&decompressor);
    throw std::runtime_error("MJPEG dimensions differ from negotiated V4L2 dimensions");
  }

  const auto components = output_encoding == "mono8" ? 1u : 3u;
  const auto row_size = static_cast<std::size_t>(decompressor.output_width) * components;
  row_data = static_cast<std::uint8_t *>(std::malloc(row_size));
  if (row_data == nullptr) {
    jpeg_destroy_decompress(&decompressor);
    throw std::bad_alloc();
  }

  while (decompressor.output_scanline < decompressor.output_height) {
    JSAMPROW row_pointer = row_data;
    jpeg_read_scanlines(&decompressor, &row_pointer, 1);
    const auto row_index = decompressor.output_scanline - 1;
    auto * destination = output + static_cast<std::size_t>(row_index) * row_size;
    if (output_encoding == "bgr8") {
      for (std::uint32_t x = 0; x < decompressor.output_width; ++x) {
        destination[x * 3] = row_data[x * 3 + 2];
        destination[x * 3 + 1] = row_data[x * 3 + 1];
        destination[x * 3 + 2] = row_data[x * 3];
      }
    } else {
      std::memcpy(destination, row_data, row_size);
    }
  }

  jpeg_finish_decompress(&decompressor);
  jpeg_destroy_decompress(&decompressor);
  std::free(row_data);
}

}  // namespace

std::size_t output_bytes_per_pixel(const std::string & output_encoding)
{
  if (output_encoding == "mono8") {
    return 1;
  }
  if (output_encoding == "bgr8" || output_encoding == "rgb8") {
    return 3;
  }
  throw std::invalid_argument(
    "output_encoding must be bgr8, rgb8, or mono8 (got '" + output_encoding + "')");
}

void convert_frame(
  const CapturedFrame & frame,
  const std::string & output_encoding,
  std::uint8_t * output,
  std::size_t output_size)
{
  check_output_buffer(frame, output_encoding, output, output_size);
  if (frame.data == nullptr || frame.bytes_used == 0) {
    throw std::invalid_argument("captured frame is empty");
  }

  if (frame.pixel_format == V4L2_PIX_FMT_YUYV) {
    convert_yuyv(frame, output_encoding, output);
    return;
  }
  if (frame.pixel_format == V4L2_PIX_FMT_MJPEG) {
    convert_mjpeg(frame, output_encoding, output);
    return;
  }
  throw std::runtime_error("unsupported V4L2 pixel format");
}

}  // namespace v4l2_camera
