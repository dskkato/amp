// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0.

#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "v4l2_camera/v4l2_camera_node.hpp"

namespace ros2topic_hz
{

class IntraProcessHz : public rclcpp::Node
{
public:
  explicit IntraProcessHz(const rclcpp::NodeOptions & options)
  : Node("ros2topic_intra_process_hz", options),
    last_time_(std::chrono::steady_clock::now())
  {
    subscription_ = create_subscription<sensor_msgs::msg::Image>(
      "/image_raw", rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::Image::ConstSharedPtr message) {
        ++message_count_;
        ++backend_counts_[message->data.get_backend_type()];
      });
    timer_ = create_wall_timer(std::chrono::seconds(1), [this]() {report();});
  }

private:
  void report()
  {
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - last_time_).count();
    const auto count = message_count_;
    const double rate = elapsed > 0.0 ? static_cast<double>(count) / elapsed : 0.0;
    std::string backends;
    for (const auto & [backend, backend_count] : backend_counts_) {
      if (!backends.empty()) {
        backends += ", ";
      }
      backends += backend + ": " + std::to_string(backend_count);
    }
    if (backends.empty()) {
      backends = "none";
    }
    RCLCPP_INFO(
      get_logger(), "received: %zu, rate: %.3f Hz, backends: %s, elapsed: %.6f s",
      count, rate, backends.c_str(), elapsed);
    message_count_ = 0;
    backend_counts_.clear();
    last_time_ = now;
  }

  std::size_t message_count_{0};
  std::map<std::string, std::size_t> backend_counts_;
  std::chrono::steady_clock::time_point last_time_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace ros2topic_hz

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);

  rclcpp::NodeOptions camera_options;
  camera_options.use_intra_process_comms(true);
  camera_options.parameter_overrides({
    rclcpp::Parameter("use_shared_buffer", false),
    rclcpp::Parameter("video_device", "/dev/video0"),
    rclcpp::Parameter("pixel_format", "YUYV"),
    rclcpp::Parameter("image_width", 640),
    rclcpp::Parameter("image_height", 480),
    rclcpp::Parameter("frame_rate", 30.0),
  });

  rclcpp::NodeOptions monitor_options;
  monitor_options.use_intra_process_comms(true);

  auto camera = std::make_shared<v4l2_camera::V4L2CameraNode>(camera_options);
  auto monitor = std::make_shared<ros2topic_hz::IntraProcessHz>(monitor_options);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(camera);
  executor.add_node(monitor);
  executor.spin();

  rclcpp::shutdown();
  return 0;
}
