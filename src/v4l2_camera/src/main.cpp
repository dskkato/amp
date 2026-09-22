// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.

#include "v4l2_camera/v4l2_camera_node.hpp"

#include <exception>
#include <iostream>
#include <memory>

#include "rclcpp/rclcpp.hpp"

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    auto node = std::make_shared<v4l2_camera::V4L2CameraNode>();
    rclcpp::spin(node);
  } catch (const std::exception & error) {
    std::cerr << "v4l2_camera failed: " << error.what() << std::endl;
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
