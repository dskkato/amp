// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0.

#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp/subscription_options.hpp"
#include "sensor_msgs/msg/image.hpp"

namespace v4l2_camera
{

class InterProcessHz : public rclcpp::Node
{
public:
  InterProcessHz(
    const std::string & topic, double interval, const std::string & acceptable_buffer_backend)
  : Node("v4l2_camera_inter_process_hz"),
    topic_(topic),
    interval_(interval),
    last_time_(std::chrono::steady_clock::now())
  {
    rclcpp::SubscriptionOptions options;
    if (!acceptable_buffer_backend.empty()) {
      options.acceptable_buffer_backends = acceptable_buffer_backend;
    }

    subscription_ = create_subscription<sensor_msgs::msg::Image>(
      topic_, rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::Image::ConstSharedPtr) {++message_count_;}, options);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(interval_)),
      [this]() {report();});

    RCLCPP_INFO(
      get_logger(), "measuring '%s', interval %.3f s%s", topic_.c_str(), interval_,
      acceptable_buffer_backend.empty() ? "" :
      (", acceptable buffer backend: " + acceptable_buffer_backend).c_str());
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

  std::string topic_;
  double interval_;
  std::size_t message_count_{0};
  std::chrono::steady_clock::time_point last_time_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace v4l2_camera

namespace
{
void print_usage(const char * program_name)
{
  std::cout << "Usage: " << program_name
            << " [--topic <topic>] [--interval <seconds>]"
            << " [--accept-buffer-backend <backend>]" << std::endl;
}
}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  const auto remaining_args = rclcpp::remove_ros_arguments(argc, argv);

  std::string topic = "/image_raw";
  std::string acceptable_buffer_backend;
  double interval = 1.0;
  for (std::size_t i = 0; i < remaining_args.size(); ++i) {
    if (remaining_args[i] == "--topic" || remaining_args[i] == "--accept-buffer-backend" ||
      remaining_args[i] == "--interval")
    {
      if (i + 1 >= remaining_args.size()) {
        std::cerr << "Missing value for " << remaining_args[i] << std::endl;
        print_usage(argv[0]);
        rclcpp::shutdown();
        return 1;
      }
      if (remaining_args[i] == "--topic") {
        topic = remaining_args[++i];
      } else if (remaining_args[i] == "--accept-buffer-backend") {
        acceptable_buffer_backend = remaining_args[++i];
      } else {
        try {
          interval = std::stod(remaining_args[++i]);
        } catch (const std::exception &) {
          std::cerr << "Invalid --interval value" << std::endl;
          print_usage(argv[0]);
          rclcpp::shutdown();
          return 1;
        }
      }
    } else if (remaining_args[i] == "--help" || remaining_args[i] == "-h") {
      print_usage(argv[0]);
      rclcpp::shutdown();
      return 0;
    }
  }

  if (interval <= 0.0) {
    std::cerr << "--interval must be positive" << std::endl;
    print_usage(argv[0]);
    rclcpp::shutdown();
    return 1;
  }

  auto node = std::make_shared<v4l2_camera::InterProcessHz>(
    topic, interval, acceptable_buffer_backend);
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
