// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#ifndef V4L2_CAMERA__V4L2_CAMERA_NODE_HPP_
#define V4L2_CAMERA__V4L2_CAMERA_NODE_HPP_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "v4l2_camera/v4l2_capture.hpp"
#include "rclcpp/node.hpp"
#include "rclcpp/publisher.hpp"
#include "sensor_msgs/msg/image.hpp"

namespace v4l2_camera
{

class V4L2CameraNode : public rclcpp::Node
{
public:
  explicit V4L2CameraNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());
  ~V4L2CameraNode() override;

private:
  using Clock = std::chrono::steady_clock;

  struct FrameTiming
  {
    Clock::duration allocation{};
    Clock::duration conversion{};
    Clock::duration publish{};
  };

  struct TimingAccumulator
  {
    std::uint64_t frames{0};
    Clock::duration cycle_total{};
    Clock::duration cycle_max{};
    Clock::duration capture_wait_total{};
    Clock::duration capture_wait_max{};
    Clock::duration allocation_total{};
    Clock::duration allocation_max{};
    Clock::duration conversion_total{};
    Clock::duration conversion_max{};
    Clock::duration publish_total{};
    Clock::duration publish_max{};
    Clock::duration requeue_total{};
    Clock::duration requeue_max{};
  };

  void capture_loop();
  void publish_frame(const CapturedFrame & frame, FrameTiming & timing);
  void report_timing();

  std::string device_;
  std::string topic_;
  std::string frame_id_;
  std::string output_encoding_;
  bool use_shmem_buffer_{true};
  bool diagnostics_{false};
  std::uint64_t diagnostics_period_frames_{30};
  int poll_timeout_ms_{1000};

  std::unique_ptr<V4L2Capture> capture_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher_;
  std::atomic_bool stopping_{false};
  std::thread capture_thread_;
  TimingAccumulator timing_;
  Clock::time_point previous_loop_start_{};
  bool has_previous_loop_start_{false};
};

}  // namespace v4l2_camera

#endif  // V4L2_CAMERA__V4L2_CAMERA_NODE_HPP_
