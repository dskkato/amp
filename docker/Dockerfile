# ROS 2 Lyrical (Ubuntu 26.04 Resolute) + CUDA development environment.
# The host only needs Docker and an NVIDIA Container Toolkit-enabled runtime.

ARG ROS_IMAGE=ros:lyrical-ros-base-resolute
FROM ${ROS_IMAGE}

ARG ROS_DISTRO=lyrical
ARG CUDA_REPO_VERSION=13-4
ARG CUDA_KEYRING_SHA256=f7f474b5f6a4adf987aa587920df00e713285958ef6a913dda1945a544a3099e
ARG PYTORCH_VERSION=2.14.0
# PyTorch 2.14.0 currently has no official cu134 Python wheel. cu132 is the
# newest CUDA-enabled wheel available for this version; the system toolkit
# remains CUDA 13.4 for the ROS/CUDA C++ packages.
ARG PYTORCH_INDEX_URL=https://download.pytorch.org/whl/cu132
ARG TARGETARCH

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# Keep the ROS base image as the distro source of ROS packages. The NVIDIA
# repository adds the compiler/toolkit only; it does not install a host driver
# into the image.
RUN test "${TARGETARCH:-amd64}" = "amd64" \
    || (echo "This image currently supports linux/amd64 only" >&2 && exit 1) \
    && export DEBIAN_FRONTEND=noninteractive \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
      build-essential \
      ca-certificates \
      curl \
      gnupg \
      libjpeg-dev \
      python3-pip \
      python3-venv \
    && curl -fsSLo /tmp/cuda-keyring.deb \
      "https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2604/x86_64/cuda-keyring_1.1-1_all.deb" \
    && echo "${CUDA_KEYRING_SHA256}  /tmp/cuda-keyring.deb" | sha256sum -c - \
    && dpkg -i /tmp/cuda-keyring.deb \
    && rm -f /tmp/cuda-keyring.deb \
    && apt-get update \
    && apt-get install -y --no-install-recommends "cuda-toolkit-${CUDA_REPO_VERSION}" \
    && rm -rf /var/lib/apt/lists/*

# Install Python PyTorch as well as the C++ headers/libraries shipped in the
# wheel. libtorch_vendor discovers this installation through Torch_DIR, so a
# second multi-gigabyte LibTorch archive is not downloaded during colcon build.
# Keep pip's downloads in a BuildKit cache mount so a later user-creation
# failure does not force the large PyTorch downloads to start from zero.
RUN --mount=type=cache,target=/root/.cache/pip \
    python3 -m pip install --break-system-packages \
      "torch==${PYTORCH_VERSION}" \
      --index-url "${PYTORCH_INDEX_URL}"

# Resolve workspace dependencies while building the image. The source tree is
# copied only for rosdep; the live workspace is still bind-mounted by Compose.
COPY src/ /tmp/amp-rosdep/src/
RUN source "/opt/ros/${ROS_DISTRO}/setup.bash" \
    && rosdep update --rosdistro "${ROS_DISTRO}" \
    && apt-get update \
    && rosdep install \
      --from-paths /tmp/amp-rosdep/src \
      --ignore-src \
      --rosdistro "${ROS_DISTRO}" \
      --skip-keys "nvidia-cuda" \
      -r -y \
    && rm -rf /tmp/amp-rosdep /var/lib/apt/lists/*

ARG DEV_USER=ubuntu
ARG DEV_UID=1000
ARG DEV_GID=1000

RUN if getent passwd "${DEV_UID}" >/dev/null; then \
      existing_user="$(getent passwd "${DEV_UID}" | cut -d: -f1)"; \
      test "${existing_user}" = "${DEV_USER}" \
        || (echo "UID ${DEV_UID} belongs to ${existing_user}; set DEV_USER accordingly" >&2 && exit 1); \
    else \
      if getent group "${DEV_GID}" >/dev/null; then \
        existing_group="$(getent group "${DEV_GID}" | cut -d: -f1)"; \
      else \
        groupadd --gid "${DEV_GID}" "${DEV_USER}"; \
        existing_group="${DEV_USER}"; \
      fi; \
      useradd --uid "${DEV_UID}" --gid "${existing_group}" --create-home \
        --shell /bin/bash "${DEV_USER}"; \
    fi \
    && mkdir -p \
      /workspaces/amp/build \
      /workspaces/amp/install \
      /workspaces/amp/log \
      "/home/${DEV_USER}/.colcon" \
      "/home/${DEV_USER}/.ros" \
    && chown -R "${DEV_UID}:${DEV_GID}" \
      /workspaces/amp \
      "/home/${DEV_USER}"

WORKDIR /workspaces/amp

ENV ROS_DISTRO=${ROS_DISTRO} \
    CUDA_HOME=/usr/local/cuda \
    PATH=/usr/local/cuda/bin:/opt/ros/${ROS_DISTRO}/bin:${PATH} \
    LD_LIBRARY_PATH=/usr/local/cuda/lib64:/opt/ros/${ROS_DISTRO}/lib \
    NVIDIA_REQUIRE_CUDA="cuda>=13.4" \
    HOME=/home/${DEV_USER}

USER ${DEV_USER}

CMD ["bash"]
