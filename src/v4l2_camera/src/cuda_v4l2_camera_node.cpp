// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0.

#include "v4l2_camera/yuyv_to_bgr.hpp"

#include <chrono>
#include <atomic>
#include <cstdint>
#include <linux/videodev2.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "cuda_buffer/cuda_buffer_api.hpp"
#include "cuda_buffer/cuda_error.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/register_node_macro.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "v4l2_camera/v4l2_capture.hpp"

namespace v4l2_camera
{

class CudaV4L2CameraNode : public rclcpp::Node
{
public:
  explicit CudaV4L2CameraNode(const rclcpp::NodeOptions & options)
  : Node("v4l2_camera_cuda", options)
  {
    device_ = declare_parameter<std::string>("video_device", "/dev/video0");
    topic_ = declare_parameter<std::string>("topic", "image_raw");
    frame_id_ = declare_parameter<std::string>("frame_id", "camera_optical_frame");
    const auto width = declare_parameter<int>("image_width", 640);
    const auto height = declare_parameter<int>("image_height", 480);
    const auto frame_rate = declare_parameter<double>("frame_rate", 30.0);
    const auto pixel_format = declare_parameter<std::string>("pixel_format", "YUYV");
    poll_timeout_ms_ = declare_parameter<int>("poll_timeout_ms", 1000);
    diagnostics_ = declare_parameter<bool>("diagnostics", false);
    diagnostics_period_frames_ = declare_parameter<int>("diagnostics_period_frames", 30);

    if (width <= 0 || height <= 0 || poll_timeout_ms_ <= 0 || diagnostics_period_frames_ <= 0) {
      throw std::invalid_argument("invalid CUDA camera parameters");
    }
    if (pixel_format != "YUYV" && pixel_format != "yuyv") {
      throw std::invalid_argument("CUDA camera currently supports pixel_format:=YUYV only");
    }

    capture_ = std::make_unique<V4L2Capture>();
    capture_->open(
      device_, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
      frame_rate, "YUYV");
    if (capture_->pixel_format() != V4L2_PIX_FMT_YUYV) {
      throw std::runtime_error("camera did not negotiate YUYV");
    }

    CUDA_CHECK(cudaStreamCreate(&stream_));
    converter_ = std::make_unique<YuyvToBgrCuda>(
      capture_->width(), capture_->height(), capture_->bytes_per_line());
    CUDA_CHECK(cudaEventCreate(&conversion_start_event_));
    CUDA_CHECK(cudaEventCreate(&conversion_end_event_));
    publisher_ = create_publisher<sensor_msgs::msg::Image>(topic_, rclcpp::SensorDataQoS());

    RCLCPP_INFO(
      get_logger(), "capturing %s at %ux%u (YUYV), publishing '%s' as CUDA bgr8",
      device_.c_str(), capture_->width(), capture_->height(), publisher_->get_topic_name());
    capture_thread_ = std::thread(&CudaV4L2CameraNode::capture_loop, this);
  }

  ~CudaV4L2CameraNode() override
  {
    stopping_.store(true);
    if (capture_thread_.joinable()) {
      capture_thread_.join();
    }
    converter_.reset();
    if (conversion_start_event_ != nullptr) {
      (void)cudaEventDestroy(conversion_start_event_);
      conversion_start_event_ = nullptr;
    }
    if (conversion_end_event_ != nullptr) {
      (void)cudaEventDestroy(conversion_end_event_);
      conversion_end_event_ = nullptr;
    }
    if (stream_ != nullptr) {
      (void)cudaStreamDestroy(stream_);
      stream_ = nullptr;
    }
    capture_.reset();
  }

private:
  using Clock = std::chrono::steady_clock;

  struct Timing
  {
    Clock::duration cycle{};
    Clock::duration capture_wait{};
    Clock::duration allocation{};
    Clock::duration launch{};
    Clock::duration synchronize{};
    Clock::duration publish{};
    float gpu_conversion_ms{0.0F};
  };

  void capture_loop()
  {
    Clock::time_point previous_loop{};
    std::uint64_t frames = 0;
    Clock::duration cycle_total{};
    Clock::duration capture_total{};
    Clock::duration allocation_total{};
    Clock::duration launch_total{};
    Clock::duration sync_total{};
    Clock::duration publish_total{};
    double gpu_conversion_total_ms = 0.0;

    while (!stopping_.load()) {
      const auto loop_start = Clock::now();
      const auto cycle = previous_loop == Clock::time_point{} ? Clock::duration{} :
        loop_start - previous_loop;
      previous_loop = loop_start;

      CapturedFrame frame;
      const auto capture_start = Clock::now();
      try {
        if (!capture_->dequeue(frame, poll_timeout_ms_)) {
          continue;
        }
      } catch (const std::exception & error) {
        RCLCPP_ERROR(get_logger(), "frame capture failed: %s", error.what());
        break;
      }

      Timing timing;
      timing.cycle = cycle;
      timing.capture_wait = Clock::now() - capture_start;
      try {
        const auto byte_count = static_cast<std::size_t>(frame.width) * frame.height * 3u;
        sensor_msgs::msg::Image message;
        message.header.stamp = now();
        message.header.frame_id = frame_id_;
        message.height = frame.height;
        message.width = frame.width;
        message.encoding = "bgr8";
        message.step = frame.width * 3u;

        const auto allocation_start = Clock::now();
        message.data = cuda_buffer_backend::allocate_buffer(byte_count);
        timing.allocation = Clock::now() - allocation_start;

        const auto launch_start = Clock::now();
        {
          auto write = cuda_buffer_backend::from_output_buffer(message.data, stream_);
          CUDA_CHECK(cudaEventRecord(conversion_start_event_, stream_));
          converter_->convert(frame, write.get_ptr(), stream_);
          CUDA_CHECK(cudaEventRecord(conversion_end_event_, stream_));
        }
        timing.launch = Clock::now() - launch_start;

        const auto publish_start = Clock::now();
        publisher_->publish(std::move(message));
        timing.publish = Clock::now() - publish_start;

        const auto sync_start = Clock::now();
        CUDA_CHECK(cudaStreamSynchronize(stream_));
        timing.synchronize = Clock::now() - sync_start;
        CUDA_CHECK(cudaEventElapsedTime(
          &timing.gpu_conversion_ms, conversion_start_event_, conversion_end_event_));
      } catch (const std::exception & error) {
        RCLCPP_ERROR(get_logger(), "CUDA conversion or publish failed: %s", error.what());
      }

      try {
        capture_->queue(frame);
      } catch (const std::exception & error) {
        RCLCPP_ERROR(get_logger(), "failed to requeue V4L2 buffer: %s", error.what());
        break;
      }

      if (diagnostics_) {
        ++frames;
        cycle_total += timing.cycle;
        capture_total += timing.capture_wait;
        allocation_total += timing.allocation;
        launch_total += timing.launch;
        sync_total += timing.synchronize;
        publish_total += timing.publish;
        gpu_conversion_total_ms += timing.gpu_conversion_ms;
        if (frames >= static_cast<std::uint64_t>(diagnostics_period_frames_)) {
          const auto avg_ms = [](Clock::duration value, std::uint64_t count) {
              return std::chrono::duration<double, std::milli>(value).count() /
                     static_cast<double>(count);
            };
          RCLCPP_INFO(
            get_logger(),
            "CUDA timing over %llu frames: cycle=%.3f ms, capture=%.3f ms, "
            "allocation=%.3f ms, gpu_conversion=%.3f ms, kernel_launch=%.3f ms, "
            "synchronize=%.3f ms, publish=%.3f ms",
            static_cast<unsigned long long>(frames), avg_ms(cycle_total, frames),
            avg_ms(capture_total, frames), avg_ms(allocation_total, frames),
            gpu_conversion_total_ms / static_cast<double>(frames),
            avg_ms(launch_total, frames), avg_ms(sync_total, frames),
            avg_ms(publish_total, frames));
          frames = 0;
          cycle_total = Clock::duration{};
          capture_total = Clock::duration{};
          allocation_total = Clock::duration{};
          launch_total = Clock::duration{};
          sync_total = Clock::duration{};
          publish_total = Clock::duration{};
          gpu_conversion_total_ms = 0.0;
        }
      }
    }
  }

  std::string device_;
  std::string topic_;
  std::string frame_id_;
  int poll_timeout_ms_{1000};
  bool diagnostics_{false};
  int diagnostics_period_frames_{30};
  std::unique_ptr<V4L2Capture> capture_;
  std::unique_ptr<YuyvToBgrCuda> converter_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr publisher_;
  cudaStream_t stream_{nullptr};
  cudaEvent_t conversion_start_event_{nullptr};
  cudaEvent_t conversion_end_event_{nullptr};
  std::atomic_bool stopping_{false};
  std::thread capture_thread_;
};

}  // namespace v4l2_camera

namespace v4l2_camera
{

rclcpp::Node::SharedPtr make_cuda_v4l2_camera_node(const rclcpp::NodeOptions & options)
{
  return std::make_shared<CudaV4L2CameraNode>(options);
}

}  // namespace v4l2_camera

RCLCPP_COMPONENTS_REGISTER_NODE(v4l2_camera::CudaV4L2CameraNode)
