#!/usr/bin/env bash
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
set -euo pipefail

# shellcheck source=docker-common.sh
source "${SCRIPT_DIR}/docker-common.sh"

docker compose "${COMPOSE_ARGS[@]}" run --rm --user root dev bash -lc '
  # ROS setup files reference variables that may be unset while being sourced.
  set -eo pipefail
  source "/opt/ros/${ROS_DISTRO}/setup.bash"
  set -u
  rosdep update --rosdistro "${ROS_DISTRO}"
  apt-get update
  rosdep install \
    --from-paths src \
    --ignore-src \
    --rosdistro "${ROS_DISTRO}" \
    --skip-keys "nvidia-cuda" \
    -r -y
  colcon build \
    --symlink-install \
    --event-handlers console_cohesion+ \
    --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo
  chown -R "${DEV_UID}:${DEV_GID}" \
    build install log \
    "/home/${DEV_USER}/.colcon" \
    "/home/${DEV_USER}/.ros"
'
