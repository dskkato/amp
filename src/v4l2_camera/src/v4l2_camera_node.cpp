// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "v4l2_camera/v4l2_camera_node.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <linux/videodev2.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "v4l2_camera/pixel_converter.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "shmem_buffer/shmem_buffer_api.hpp"

namespace v4l2_camera
{

V4L2CameraNode::V4L2CameraNode(const rclcpp::NodeOptions & options)
: Node("v4l2_camera", options)
{
  device_ = declare_parameter<std::string>("video_device", "/dev/video0");
  topic_ = declare_parameter<std::string>("topic", "image_raw");
  frame_id_ = declare_parameter<std::string>("frame_id", "camera_optical_frame");
  output_encoding_ = declare_parameter<std::string>("output_encoding", "bgr8");
  use_shmem_buffer_ = declare_parameter<bool>("use_shmem_buffer", true);
  diagnostics_ = declare_parameter<bool>("diagnostics", false);
  const auto diagnostics_period = declare_parameter<int>("diagnostics_period_frames", 30);
  const auto width = declare_parameter<int>("image_width", 640);
  const auto height = declare_parameter<int>("image_height", 480);
  const auto frame_rate = declare_parameter<double>("frame_rate", 30.0);
  const auto pixel_format = declare_parameter<std::string>("pixel_format", "auto");
  poll_timeout_ms_ = declare_parameter<int>("poll_timeout_ms", 1000);

  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("image_width and image_height must be greater than zero");
  }
  if (poll_timeout_ms_ <= 0) {
    throw std::invalid_argument("poll_timeout_ms must be greater than zero");
  }
  if (diagnostics_period <= 0) {
    throw std::invalid_argument("diagnostics_period_frames must be greater than zero");
  }
  diagnostics_period_frames_ = static_cast<std::uint64_t>(diagnostics_period);
  (void)output_bytes_per_pixel(output_encoding_);

  capture_ = std::make_unique<V4L2Capture>();
  capture_->open(
    device_, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
    frame_rate, pixel_format);

  publisher_ = create_publisher<sensor_msgs::msg::Image>(topic_, rclcpp::SensorDataQoS());

  RCLCPP_INFO(
    get_logger(),
    "capturing %s at %ux%u (%s), publishing '%s' as %s%s",
    device_.c_str(), capture_->width(), capture_->height(),
    capture_->pixel_format() == V4L2_PIX_FMT_MJPEG ? "MJPEG" : "YUYV",
    publisher_->get_topic_name(), output_encoding_.c_str(),
    use_shmem_buffer_ ? " using shmem_buffer" : "");

  capture_thread_ = std::thread(&V4L2CameraNode::capture_loop, this);
}

V4L2CameraNode::~V4L2CameraNode()
{
  stopping_.store(true);
  if (capture_thread_.joinable()) {
    capture_thread_.join();
  }
  capture_.reset();
}

void V4L2CameraNode::capture_loop()
{
  while (!stopping_.load()) {
    const auto loop_start = Clock::now();
    const auto cycle = has_previous_loop_start_ ? loop_start - previous_loop_start_ : Clock::duration{};
    previous_loop_start_ = loop_start;
    has_previous_loop_start_ = true;

    CapturedFrame frame;
    const auto capture_wait_start = Clock::now();
    try {
      if (!capture_->dequeue(frame, poll_timeout_ms_)) {
        continue;
      }
    } catch (const std::exception & error) {
      RCLCPP_ERROR(get_logger(), "frame capture failed: %s", error.what());
      break;
    }
    const auto capture_wait = Clock::now() - capture_wait_start;

    FrameTiming frame_timing;
    try {
      publish_frame(frame, frame_timing);
    } catch (const std::exception & error) {
      RCLCPP_ERROR(get_logger(), "frame conversion or publish failed: %s", error.what());
    }

    const auto requeue_start = Clock::now();
    try {
      capture_->queue(frame);
    } catch (const std::exception & error) {
      RCLCPP_ERROR(get_logger(), "failed to requeue V4L2 buffer: %s", error.what());
      break;
    }
    const auto requeue = Clock::now() - requeue_start;

    if (diagnostics_) {
      ++timing_.frames;
      timing_.cycle_total += cycle;
      timing_.cycle_max = std::max(timing_.cycle_max, cycle);
      timing_.capture_wait_total += capture_wait;
      timing_.capture_wait_max = std::max(timing_.capture_wait_max, capture_wait);
      timing_.allocation_total += frame_timing.allocation;
      timing_.allocation_max = std::max(timing_.allocation_max, frame_timing.allocation);
      timing_.conversion_total += frame_timing.conversion;
      timing_.conversion_max = std::max(timing_.conversion_max, frame_timing.conversion);
      timing_.publish_total += frame_timing.publish;
      timing_.publish_max = std::max(timing_.publish_max, frame_timing.publish);
      timing_.requeue_total += requeue;
      timing_.requeue_max = std::max(timing_.requeue_max, requeue);
      if (timing_.frames >= diagnostics_period_frames_) {
        report_timing();
      }
    }
  }
}

void V4L2CameraNode::publish_frame(const CapturedFrame & frame, FrameTiming & timing)
{
  const auto bytes_per_pixel = output_bytes_per_pixel(output_encoding_);
  const auto step = static_cast<std::size_t>(frame.width) * bytes_per_pixel;
  const auto byte_count = step * frame.height;

  sensor_msgs::msg::Image message;
  message.header.stamp = now();
  message.header.frame_id = frame_id_;
  message.height = frame.height;
  message.width = frame.width;
  message.encoding = output_encoding_;
  message.is_bigendian = 0;
  message.step = static_cast<std::uint32_t>(step);

  const auto allocation_start = Clock::now();
  if (use_shmem_buffer_) {
    message.data = shmem_buffer::allocate_buffer(byte_count);
  } else {
    message.data.resize(byte_count);
  }
  timing.allocation = Clock::now() - allocation_start;

  const auto conversion_start = Clock::now();
  if (use_shmem_buffer_) {
    auto write = shmem_buffer::from_output_buffer(message.data);
    convert_frame(frame, output_encoding_, write.get_ptr(), byte_count);
  } else {
    convert_frame(frame, output_encoding_, message.data.data(), byte_count);
  }
  timing.conversion = Clock::now() - conversion_start;

  const auto publish_start = Clock::now();
  publisher_->publish(std::move(message));
  timing.publish = Clock::now() - publish_start;
}

void V4L2CameraNode::report_timing()
{
  const auto average_ms = [](Clock::duration duration, std::uint64_t frames) {
      return frames == 0 ? 0.0 :
             std::chrono::duration<double, std::milli>(duration).count() /
             static_cast<double>(frames);
    };
  const auto max_ms = [](Clock::duration duration) {
      return std::chrono::duration<double, std::milli>(duration).count();
    };

  RCLCPP_INFO(
    get_logger(),
    "timing over %llu frames: cycle avg/max=%.3f/%.3f ms, "
    "capture_wait avg/max=%.3f/%.3f ms, allocation avg/max=%.3f/%.3f ms, "
    "conversion avg/max=%.3f/%.3f ms, publish avg/max=%.3f/%.3f ms, "
    "requeue avg/max=%.3f/%.3f ms",
    static_cast<unsigned long long>(timing_.frames),
    average_ms(timing_.cycle_total, timing_.frames), max_ms(timing_.cycle_max),
    average_ms(timing_.capture_wait_total, timing_.frames), max_ms(timing_.capture_wait_max),
    average_ms(timing_.allocation_total, timing_.frames), max_ms(timing_.allocation_max),
    average_ms(timing_.conversion_total, timing_.frames), max_ms(timing_.conversion_max),
    average_ms(timing_.publish_total, timing_.frames), max_ms(timing_.publish_max),
    average_ms(timing_.requeue_total, timing_.frames), max_ms(timing_.requeue_max));

  timing_ = TimingAccumulator{};
}

}  // namespace v4l2_camera

RCLCPP_COMPONENTS_REGISTER_NODE(v4l2_camera::V4L2CameraNode)
