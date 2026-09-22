// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0.

#include <exception>
#include <iostream>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "v4l2_camera/cuda_v4l2_camera_node.hpp"

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    auto node = v4l2_camera::make_cuda_v4l2_camera_node();
    rclcpp::spin(node);
  } catch (const std::exception & error) {
    std::cerr << "v4l2_camera_cuda failed: " << error.what() << std::endl;
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
