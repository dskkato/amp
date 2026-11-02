cmake_minimum_required(VERSION 3.20)
project(ros2topic_hz)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(v4l2_camera REQUIRED)

add_executable(ros2topic_hz src/hz.cpp)
target_link_libraries(
  ros2topic_hz
  rclcpp::rclcpp
  ${sensor_msgs_TARGETS}
)

install(
  TARGETS ros2topic_hz
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

add_executable(ros2topic_intra_process_hz src/intra_process_hz.cpp)
target_include_directories(ros2topic_intra_process_hz PRIVATE ${v4l2_camera_INCLUDE_DIRS})
target_link_libraries(
  ros2topic_intra_process_hz
  v4l2_camera::v4l2_camera_component
  rclcpp::rclcpp
  ${sensor_msgs_TARGETS}
)

install(
  TARGETS ros2topic_intra_process_hz
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)

install(
  PROGRAMS scripts/shmem_buffer_hz.py
  DESTINATION lib/${PROJECT_NAME}
  RENAME ros2topic_shmem_buffer_hz
)

include(CheckLanguage)
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
  enable_language(CUDA)
  find_package(CUDAToolkit REQUIRED)
  find_package(cuda_buffer REQUIRED)

  add_executable(ros2topic_cuda_hz src/cuda_hz.cpp)
  target_link_libraries(
    ros2topic_cuda_hz
    rclcpp::rclcpp
    ${sensor_msgs_TARGETS}
    cuda_buffer::cuda_buffer
    CUDA::cudart
  )
  set_target_properties(ros2topic_cuda_hz PROPERTIES
    CUDA_STANDARD 17
    CUDA_STANDARD_REQUIRED ON
  )

  install(
    TARGETS ros2topic_cuda_hz
    RUNTIME DESTINATION lib/${PROJECT_NAME}
  )
endif()

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
endif()

ament_export_dependencies(rclcpp sensor_msgs v4l2_camera)
ament_package()
