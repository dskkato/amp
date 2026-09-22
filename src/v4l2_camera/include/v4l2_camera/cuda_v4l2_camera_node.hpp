// Copyright 2026 Daisuke Kato
//
// Licensed under the Apache License, Version 2.0.

#ifndef V4L2_CAMERA__CUDA_V4L2_CAMERA_NODE_HPP_
#define V4L2_CAMERA__CUDA_V4L2_CAMERA_NODE_HPP_

#include "rclcpp/node.hpp"

namespace v4l2_camera
{

rclcpp::Node::SharedPtr make_cuda_v4l2_camera_node(
  const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

}  // namespace v4l2_camera

#endif  // V4L2_CAMERA__CUDA_V4L2_CAMERA_NODE_HPP_
