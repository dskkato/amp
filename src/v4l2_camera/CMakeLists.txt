cmake_minimum_required(VERSION 3.20)
project(v4l2_camera)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(shared_buffer REQUIRED)
find_package(JPEG REQUIRED)
include(CheckLanguage)
check_language(CUDA)

add_library(v4l2_camera_component SHARED
  src/pixel_converter.cpp
  src/v4l2_capture.cpp
  src/v4l2_camera_node.cpp
)

target_include_directories(v4l2_camera_component PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(v4l2_camera_component
  rclcpp::rclcpp
  rclcpp_components::component
  ${sensor_msgs_TARGETS}
  shared_buffer::shared_buffer
  JPEG::JPEG
)

rclcpp_components_register_nodes(v4l2_camera_component
  "v4l2_camera::V4L2CameraNode")

add_executable(v4l2_camera_node src/main.cpp)
target_link_libraries(v4l2_camera_node v4l2_camera_component rclcpp::rclcpp)

add_executable(v4l2_camera_intra_process_hz src/intra_process_hz.cpp)
target_link_libraries(
  v4l2_camera_intra_process_hz
  v4l2_camera_component
  rclcpp::rclcpp
  ${sensor_msgs_TARGETS}
)

add_executable(v4l2_camera_inter_process_hz src/inter_process_hz.cpp)
target_link_libraries(
  v4l2_camera_inter_process_hz
  rclcpp::rclcpp
  ${sensor_msgs_TARGETS}
)

install(
  DIRECTORY include/
  DESTINATION include
)

install(
  TARGETS
    v4l2_camera_component
    v4l2_camera_node
    v4l2_camera_intra_process_hz
    v4l2_camera_inter_process_hz
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

install(
  DIRECTORY launch/
  DESTINATION share/${PROJECT_NAME}/launch
)

install(
  PROGRAMS scripts/shared_buffer_hz.py
  DESTINATION lib/${PROJECT_NAME}
  RENAME shared_buffer_hz
)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_pixel_converter
    test/test_pixel_converter.cpp
  )
  if(TARGET test_pixel_converter)
    target_link_libraries(test_pixel_converter v4l2_camera_component)
    target_include_directories(test_pixel_converter PRIVATE include)
  endif()
endif()

ament_export_dependencies(rclcpp rclcpp_components sensor_msgs shared_buffer)
ament_export_include_directories(include)
ament_export_libraries(v4l2_camera_component)

if(CMAKE_CUDA_COMPILER)
  enable_language(CUDA)
  find_package(CUDAToolkit REQUIRED)
  find_package(cuda_buffer REQUIRED)

  add_library(v4l2_camera_cuda_component SHARED
    src/cuda_v4l2_camera_node.cpp
    src/yuyv_to_bgr.cu
  )
  target_include_directories(v4l2_camera_cuda_component PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  )
  target_link_libraries(v4l2_camera_cuda_component
    v4l2_camera_component
    rclcpp::rclcpp
    rclcpp_components::component
    ${sensor_msgs_TARGETS}
    cuda_buffer::cuda_buffer
    CUDA::cudart
  )
  set_target_properties(v4l2_camera_cuda_component PROPERTIES
    CUDA_STANDARD 17
    CUDA_STANDARD_REQUIRED ON
    CUDA_SEPARABLE_COMPILATION ON
  )
  rclcpp_components_register_nodes(v4l2_camera_cuda_component
    "v4l2_camera::CudaV4L2CameraNode")

  add_executable(v4l2_camera_cuda_node src/cuda_main.cpp)
  target_link_libraries(v4l2_camera_cuda_node
    v4l2_camera_cuda_component
    rclcpp::rclcpp
  )

  add_executable(v4l2_camera_cuda_hz src/cuda_hz.cpp)
  target_link_libraries(v4l2_camera_cuda_hz
    rclcpp::rclcpp
    ${sensor_msgs_TARGETS}
    cuda_buffer::cuda_buffer
    CUDA::cudart
  )

  install(
    TARGETS
      v4l2_camera_cuda_component
      v4l2_camera_cuda_node
      v4l2_camera_cuda_hz
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION lib/${PROJECT_NAME}
  )
endif()

ament_package()
