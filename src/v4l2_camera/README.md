# v4l2_camera

ROS 2 camera publisher for a Linux V4L2 device. It captures frames from a
laptop or USB camera and publishes `sensor_msgs/msg/Image` on `image_raw`.

Until the fix in [rosidl_typesupport_fastrtps#164](https://github.com/ros2/rosidl_typesupport_fastrtps/pull/164)
is available in the installed ROS 2 packages, the run commands below set the
publisher's `serialize_buffer_with_endpoint` or the subscriber's
`deserialize_buffer_with_endpoint` logger to `WARN` to suppress repeated Fast
CDR debug messages while preserving other loggers' levels.

The node supports YUYV and MJPEG V4L2 input. By default it converts frames to
`bgr8` and allocates the image payload with the workspace's `shared_buffer`
backend.

## Run

Build the workspace and source the generated setup file:

```bash
source /opt/ros/lyrical/setup.bash
source install/setup.bash
ros2 run v4l2_camera v4l2_camera_node --ros-args \
  --log-level serialize_buffer_with_endpoint:=warn
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
  -p use_shared_buffer:=true \
  --log-level serialize_buffer_with_endpoint:=warn
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
  -p diagnostics_period_frames:=30 \
  --log-level serialize_buffer_with_endpoint:=warn
```

## Frequency measurement

The workspace includes the `ros2topic_hz` package under `src/ros2topic_hz`.
For inter-process communication, start the camera in one process and measure
it from another process. The generic Image subscriber reports the backend
observed by each callback and can explicitly accept the `shared_buffer`
backend:

```bash
ros2 run v4l2_camera v4l2_camera_node --ros-args \
  -p pixel_format:=YUYV \
  -p use_shared_buffer:=true \
  --log-level serialize_buffer_with_endpoint:=warn

ros2 run ros2topic_hz ros2topic_hz \
  --topic /image_raw \
  --interval 1.0 \
  --accept-buffer-backend shared_buffer \
  --ros-args --log-level deserialize_buffer_with_endpoint:=warn
```

The equivalent Python measurement uses `rclpy`'s typed Image subscription
with `acceptable_buffer_backends='shared_buffer'`:

```bash
ros2 run ros2topic_hz ros2topic_shared_buffer_hz \
  --topic /image_raw \
  --interval 1.0 \
  --ros-args --log-level deserialize_buffer_with_endpoint:=warn
```

For intra-process communication, use the benchmark executable. It creates
the camera publisher and a typed image subscriber in one process, with
`use_shared_buffer=false` and the same 640x480 YUYV/30 fps settings:

```bash
ros2 run ros2topic_hz ros2topic_intra_process_hz --ros-args \
  --log-level serialize_buffer_with_endpoint:=warn \
  --log-level deserialize_buffer_with_endpoint:=warn
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
  -p diagnostics:=true \
  --log-level serialize_buffer_with_endpoint:=warn

ros2 run ros2topic_hz ros2topic_cuda_hz \
  --topic /image_raw \
  --interval 1.0 \
  --ros-args --log-level deserialize_buffer_with_endpoint:=warn
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
