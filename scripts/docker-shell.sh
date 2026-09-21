#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

# shellcheck source=docker-common.sh
source "${SCRIPT_DIR}/docker-common.sh"

exec docker compose "${COMPOSE_ARGS[@]}" run --rm dev bash "$@"
