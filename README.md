## ROS 2 Lyrical + rosidl buffer backend development

This workspace is prepared for ROS 2 Lyrical on Ubuntu 26.04 inside Docker.
The image is based on the official ROS 2 desktop image and adds the NVIDIA
CUDA toolkit (`nvcc`) plus a CUDA-enabled PyTorch wheel. The host ROS, CUDA
toolkit, PyTorch, and rosdep packages are not modified or installed.

The current image is intentionally limited to `linux/amd64`: the CUDA
repository URL and the PyTorch wheel used here are x86_64 development inputs.
The container runs as a non-root development user (`ubuntu` by default) for
normal shell, build, and test work. Dependency installation is performed as
root by the build script.

The host needs Docker Compose and an NVIDIA Container Toolkit-enabled Docker
runtime. Verify the runtime before building with `docker run --rm --gpus all
nvidia/cuda:13.4.1-base-ubuntu26.04 nvidia-smi`.

### Source layout

This is a monorepo workspace. `src/v4l2_camera` is maintained by the root
project, while the reusable buffer implementations are pinned as Git
submodules:

```text
src/
├── rosidl_buffer_backends/  # git submodule
├── shmem_buffer_backend/   # git submodule
├── ros2topic_hz/             # root-project subscriber benchmark package
└── v4l2_camera/             # root-project package
```

After cloning, initialize the external packages with:

```bash
git submodule update --init --recursive
```

Initialize the submodules before building the image. The Docker build resolves
and installs ROS package dependencies from the manifests under `src/`.

The container receives `NVIDIA_VISIBLE_DEVICES=all` and
`NVIDIA_DRIVER_CAPABILITIES=compute,utility` from Compose. Override them with
`AMP_NVIDIA_VISIBLE_DEVICES` or `AMP_NVIDIA_DRIVER_CAPABILITIES` when needed.
The Compose file reserves all GPUs using the standard device reservation
syntax. The image also declares a CUDA driver compatibility constraint.

By default the container uses host networking and ROS subnet discovery, which
allows DDS discovery between the container and host/network peers on Linux.
For isolated local tests, use:

```bash
AMP_NETWORK_MODE=bridge ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST \
  ./scripts/docker-colcon-test.sh
```

Set `ROS_DOMAIN_ID` when the container must be isolated from other ROS 2
systems. `ROS_STATIC_PEERS` can be used for explicitly configured peers.

### Build the development image

```bash
./scripts/docker-build.sh
```

The CUDA and PyTorch versions can be overridden without editing files:

```bash
CUDA_REPO_VERSION=13-4 PYTORCH_VERSION=2.14.0 ./scripts/docker-build.sh
```

PyTorch is installed intentionally. `libtorch_vendor` discovers the
`TorchConfig.cmake` shipped inside the Python wheel and reuses it for the C++
build, while Python-based shared-buffer experiments can import `torch` at
runtime. If the wheel is removed, the CMake vendor package falls back to
downloading a separate LibTorch archive during `colcon build`. The image uses
CUDA Toolkit 13.4 and PyTorch 2.14.0. The official Python wheel currently uses
the `cu132` index, while the vendor fallback selects the matching
`libtorch 2.14.0+cu134` archive when no Python-provided TorchConfig is found.

The ROS image and distro are configurable together when using a compatible
image. Keep these values consistent:

```bash
ROS_DISTRO=lyrical \
ROS_IMAGE=osrf/ros:lyrical-desktop-resolute \
  ./scripts/docker-build.sh
```

### Resolve dependencies and build

```bash
./scripts/docker-colcon-build.sh
```

This builds the complete workspace with dependencies already installed in the
Docker image. `rosdep update` and `rosdep install` run during `docker-build.sh`,
using the package manifests under `src/`. This includes
`ament_cmake_vendor_package` through rosdep. The `nvidia-cuda` rosdep key is
skipped because CUDA is installed explicitly in the image. `pybind11-dev` is
resolved normally through rosdep/apt. Rebuild the image after changing package
dependencies or rosdep manifests.

### Run tests

```bash
./scripts/docker-colcon-test.sh
```

The CUDA tests use the GPU when the container is started with the NVIDIA
runtime. Tests that require only CPU/shared memory still run in the same
workspace and use the mounted named Docker volumes for `build/`, `install/`,
and `log/`, backed by Docker named volumes so generated artifacts do not need
to be installed on the host.

The named volumes are scoped under the Compose project name `amp`. Override
that name with `COMPOSE_PROJECT_NAME` when multiple independent copies of the
workspace must coexist.

### Open a shell

```bash
./scripts/docker-shell.sh
```

For RViz/Gazebo-style X11 applications on Linux, enable the GUI overlay:

```bash
AMP_GUI=1 ./scripts/docker-shell.sh
```

The overlay mounts `/tmp/.X11-unix` and adds the NVIDIA graphics/display
capabilities. The host X server must still permit the container connection
(for example through an appropriate `xhost` policy).

Inside the shell, the usual environment is available after:

```bash
source "/opt/ros/${ROS_DISTRO:-lyrical}/setup.bash"
source install/setup.bash
nvcc --version
python3 -c 'import torch; print(torch.__version__, torch.cuda.is_available())'
```
