# v4l2_camera

ROS 2 camera publisher for a Linux V4L2 device. It captures frames from a
laptop or USB camera and publishes `sensor_msgs/msg/Image` on `image_raw`.

The node supports YUYV and MJPEG V4L2 input. By default it converts frames to
`bgr8` and allocates the image payload with the workspace's `shared_buffer`
backend.

## Run

Build the workspace and source the generated setup file:

```bash
source /opt/ros/lyrical/setup.bash
source install/setup.bash
ros2 run v4l2_camera v4l2_camera_node
```

The default device is `/dev/video0`, and the default topic is
`/image_raw`. Common parameters are:

```bash
ros2 run v4l2_camera v4l2_camera_node --ros-args \
  -p video_device:=/dev/video0 \
  -p image_width:=640 \
  -p image_height:=480 \
  -p frame_rate:=30.0 \
  -p output_encoding:=bgr8 \
  -p use_shared_buffer:=true
```

The launch file exposes the same camera parameters:

```bash
ros2 launch v4l2_camera v4l2_camera.launch.py
```

For timing diagnostics, enable the optional per-stage measurement. The node
reports the capture-loop period and the average/maximum time spent waiting for
V4L2, allocating the image buffer, converting pixels, publishing, and
re-queueing the V4L2 buffer:

```bash
ros2 run v4l2_camera v4l2_camera_node --ros-args \
  -p diagnostics:=true \
  -p diagnostics_period_frames:=30
```

## Frequency measurement

The workspace includes the C++ `ros2topic_hz` package under `src/ros2topic_hz`.
For inter-process communication, start the camera in one process and measure
it from another process. The typed subscriber can explicitly accept the
`shared_buffer` backend:

```bash
ros2 run v4l2_camera v4l2_camera_node --ros-args \
  -p pixel_format:=YUYV \
  -p use_shared_buffer:=true

ros2 run v4l2_camera v4l2_camera_inter_process_hz \
  --topic /image_raw \
  --interval 1.0 \
  --accept-buffer-backend shared_buffer
```

The equivalent Python measurement uses `rclpy`'s typed subscription with
`acceptable_buffer_backends='shared_buffer'`. It reports both the rate and
the backend observed by each callback:

```bash
ros2 run v4l2_camera shared_buffer_hz \
  --topic /image_raw \
  --interval 1.0
```

For intra-process communication, use the benchmark executable. It creates
the camera publisher and a typed image subscriber in one process, with
`use_shared_buffer=false` and the same 640x480 YUYV/30 fps settings:

```bash
ros2 run v4l2_camera v4l2_camera_intra_process_hz
```

## CUDA path

When a CUDA compiler and the workspace `cuda_buffer` package are available,
the package also builds a CUDA camera node. It copies the captured YUYV frame
to device memory, converts YUYV to BGR8 in a CUDA kernel, and publishes the
result using the CUDA buffer backend. The subscriber benchmark accepts that
backend, performs a Device-to-Host copy, and reports its CPU-side copy time:

```bash
ros2 run v4l2_camera v4l2_camera_cuda_node --ros-args \
  -p video_device:=/dev/video0 \
  -p pixel_format:=YUYV \
  -p frame_rate:=30.0 \
  -p diagnostics:=true

ros2 run v4l2_camera v4l2_camera_cuda_hz \
  --topic /image_raw \
  --interval 1.0
```

The camera diagnostics include the GPU event time covering the Host-to-Device
copy and YUYV-to-BGR8 kernel, separately from the CPU enqueue and publish
times.

## Docker device access

The workspace Compose file passes `/dev/video0` to the development container.
If the camera uses another device or the host's `video` group has a different
numeric GID, override these variables:

```bash
AMP_CAMERA_DEVICE=/dev/video2 AMP_VIDEO_GID=44 ./scripts/docker-shell.sh
```

Use `v4l2-ctl --list-devices` and `stat -c '%g' /dev/video0` on the host to
inspect the device and group ID.
