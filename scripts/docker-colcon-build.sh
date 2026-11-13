#!/usr/bin/env bash
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
set -euo pipefail

# shellcheck source=docker-common.sh
source "${SCRIPT_DIR}/docker-common.sh"

docker compose "${COMPOSE_ARGS[@]}" run --rm dev bash -lc '
  # ROS setup files reference variables that may be unset while being sourced.
  set -eo pipefail
  source "/opt/ros/${ROS_DISTRO}/setup.bash"
  set -u

  # The build directory is persisted on the host. If a package has moved,
  # discard only its stale CMake build directory so CMake can configure it
  # from its current source path.
  for cache in build/*/CMakeCache.txt; do
    [[ -f "${cache}" ]] || continue
    source_dir="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${cache}" | head -n 1)"
    if [[ -n "${source_dir}" && ! -d "${source_dir}" ]]; then
      rm -rf -- "${cache%/CMakeCache.txt}"
    fi
  done

  colcon build \
    --symlink-install \
    --event-handlers console_cohesion+ \
    --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo
'
