// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0.

#include <cuda_runtime.h>

#include <chrono>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cuda_buffer/cuda_buffer_api.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp/subscription_options.hpp"
#include "sensor_msgs/msg/image.hpp"

namespace v4l2_camera
{

class CudaHz : public rclcpp::Node
{
public:
  CudaHz(const std::string & topic, double interval)
  : Node("v4l2_camera_cuda_hz"), last_time_(std::chrono::steady_clock::now())
  {
    CUDA_CHECK(cudaStreamCreate(&stream_));
    rclcpp::SubscriptionOptions options;
    options.acceptable_buffer_backends = "cuda";
    subscription_ = create_subscription<sensor_msgs::msg::Image>(
      topic, rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::Image::ConstSharedPtr message) {callback(std::move(message));},
      options);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(interval)),
      [this]() {report();});
    RCLCPP_INFO(
      get_logger(), "measuring '%s', interval %.3f s, acceptable buffer backend: cuda",
      topic.c_str(), interval);
  }

  ~CudaHz() override
  {
    if (stream_ != nullptr) {
      (void)cudaStreamSynchronize(stream_);
      (void)cudaStreamDestroy(stream_);
    }
  }

private:
  using Clock = std::chrono::steady_clock;

  void callback(sensor_msgs::msg::Image::ConstSharedPtr message)
  {
    const auto start = Clock::now();
    try {
      if (message->data.get_backend_type() != "cuda") {
        ++other_backend_count_;
        return;
      }
      const auto bytes = message->data.size();
      host_data_.resize(bytes);
      const auto read = cuda_buffer_backend::from_input_buffer(message->data, stream_);
      CUDA_CHECK(cudaMemcpyAsync(
        host_data_.data(), read.get_ptr(), bytes, cudaMemcpyDeviceToHost, stream_));
      CUDA_CHECK(cudaStreamSynchronize(stream_));
      const auto elapsed = Clock::now() - start;
      ++count_;
      copy_total_ += elapsed;
      copy_max_ = std::max(copy_max_, elapsed);
    } catch (const std::exception & error) {
      ++error_count_;
      RCLCPP_ERROR_THROTTLE(
        get_logger(), *get_clock(), 1000, "CUDA receive/copy failed: %s", error.what());
    }
  }

  void report()
  {
    const auto now = Clock::now();
    const double elapsed = std::chrono::duration<double>(now - last_time_).count();
    const double rate = elapsed > 0.0 ? static_cast<double>(count_) / elapsed : 0.0;
    const double average_copy = count_ == 0 ? 0.0 :
      std::chrono::duration<double, std::milli>(copy_total_).count() /
      static_cast<double>(count_);
    const double max_copy = std::chrono::duration<double, std::milli>(copy_max_).count();
    RCLCPP_INFO(
      get_logger(),
      "received: %zu, rate: %.3f Hz, CPU copy avg/max: %.3f/%.3f ms, "
      "other_backend: %zu, errors: %zu",
      count_, rate, average_copy, max_copy, other_backend_count_, error_count_);
    count_ = 0;
    other_backend_count_ = 0;
    error_count_ = 0;
    copy_total_ = Clock::duration{};
    copy_max_ = Clock::duration{};
    last_time_ = now;
  }

  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
  rclcpp::TimerBase::SharedPtr timer_;
  cudaStream_t stream_{nullptr};
  std::vector<std::uint8_t> host_data_;
  std::size_t count_{0};
  std::size_t other_backend_count_{0};
  std::size_t error_count_{0};
  Clock::duration copy_total_{};
  Clock::duration copy_max_{};
  Clock::time_point last_time_;
};

}  // namespace v4l2_camera

namespace
{
void print_usage(const char * program_name)
{
  std::cout << "Usage: " << program_name
            << " [--topic <topic>] [--interval <seconds>]" << std::endl;
}
}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  const auto remaining_args = rclcpp::remove_ros_arguments(argc, argv);
  std::string topic = "/image_raw";
  double interval = 1.0;
  for (std::size_t i = 0; i < remaining_args.size(); ++i) {
    if (remaining_args[i] == "--topic" || remaining_args[i] == "--interval") {
      if (i + 1 >= remaining_args.size()) {
        print_usage(argv[0]);
        rclcpp::shutdown();
        return 1;
      }
      if (remaining_args[i] == "--topic") {
        topic = remaining_args[++i];
      } else {
        interval = std::stod(remaining_args[++i]);
      }
    } else if (remaining_args[i] == "--help" || remaining_args[i] == "-h") {
      print_usage(argv[0]);
      rclcpp::shutdown();
      return 0;
    }
  }
  if (interval <= 0.0) {
    print_usage(argv[0]);
    rclcpp::shutdown();
    return 1;
  }

  auto node = std::make_shared<v4l2_camera::CudaHz>(topic, interval);
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
