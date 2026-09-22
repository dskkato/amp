// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0.

#include <chrono>
#include <cstddef>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "v4l2_camera/v4l2_camera_node.hpp"

namespace v4l2_camera
{

class IntraProcessHz : public rclcpp::Node
{
public:
  explicit IntraProcessHz(const rclcpp::NodeOptions & options)
  : Node("v4l2_camera_intra_process_hz", options), last_time_(std::chrono::steady_clock::now())
  {
    subscription_ = create_subscription<sensor_msgs::msg::Image>(
      "/image_raw", rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::Image::ConstSharedPtr) {++message_count_;});
    timer_ = create_wall_timer(std::chrono::seconds(1), [this]() {report();});
  }

private:
  void report()
  {
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - last_time_).count();
    const auto count = message_count_;
    const double rate = elapsed > 0.0 ? static_cast<double>(count) / elapsed : 0.0;
    RCLCPP_INFO(
      get_logger(), "received: %zu, rate: %.3f Hz, elapsed: %.6f s", count, rate, elapsed);
    message_count_ = 0;
    last_time_ = now;
  }

  std::size_t message_count_{0};
  std::chrono::steady_clock::time_point last_time_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace v4l2_camera

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
  auto monitor = std::make_shared<v4l2_camera::IntraProcessHz>(monitor_options);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(camera);
  executor.add_node(monitor);
  executor.spin();

  rclcpp::shutdown();
  return 0;
}
