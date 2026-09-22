// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0.

#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp/subscription_options.hpp"
#include "sensor_msgs/msg/image.hpp"

namespace ros2topic_hz
{

class ImageHz : public rclcpp::Node
{
public:
  ImageHz(const std::string & topic, double interval, const std::string & acceptable_backend)
  : Node("ros2topic_hz"),
    topic_(topic),
    interval_(interval),
    last_time_(std::chrono::steady_clock::now())
  {
    rclcpp::SubscriptionOptions options;
    if (!acceptable_backend.empty()) {
      options.acceptable_buffer_backends = acceptable_backend;
    }

    subscription_ = create_subscription<sensor_msgs::msg::Image>(
      topic_, rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::Image::ConstSharedPtr message) {callback(std::move(message));},
      options);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(interval_)),
      [this]() {report();});

    RCLCPP_INFO(
      get_logger(), "measuring '%s', interval %.3f s%s", topic_.c_str(), interval_,
      acceptable_backend.empty() ? "" :
      (", acceptable buffer backend: " + acceptable_backend).c_str());
  }

private:
  void callback(sensor_msgs::msg::Image::ConstSharedPtr message)
  {
    ++message_count_;
    ++backend_counts_[message->data.get_backend_type()];
  }

  void report()
  {
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - last_time_).count();
    const auto count = message_count_;
    const double rate = elapsed > 0.0 ? static_cast<double>(count) / elapsed : 0.0;

    std::ostringstream backend_summary;
    bool first = true;
    for (const auto & [backend, backend_count] : backend_counts_) {
      if (!first) {
        backend_summary << ", ";
      }
      backend_summary << backend << ": " << backend_count;
      first = false;
    }
    if (first) {
      backend_summary << "none";
    }

    RCLCPP_INFO(
      get_logger(), "received: %zu, rate: %.3f Hz, backends: %s, elapsed: %.6f s",
      count, rate, backend_summary.str().c_str(), elapsed);
    message_count_ = 0;
    backend_counts_.clear();
    last_time_ = now;
  }

  std::string topic_;
  double interval_;
  std::size_t message_count_{0};
  std::map<std::string, std::size_t> backend_counts_;
  std::chrono::steady_clock::time_point last_time_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace ros2topic_hz

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
  std::string acceptable_backend;
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
        acceptable_backend = remaining_args[++i];
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

  auto node = std::make_shared<ros2topic_hz::ImageHz>(topic, interval, acceptable_backend);
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
