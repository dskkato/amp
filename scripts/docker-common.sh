#!/usr/bin/env bash

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

export DEV_UID="${DEV_UID:-$(id -u)}"
export DEV_GID="${DEV_GID:-$(id -g)}"
if [[ -z "${DEV_USER:-}" ]]; then
  if [[ "${DEV_UID}" == "1000" && "${DEV_GID}" == "1000" ]]; then
    export DEV_USER=ubuntu
  else
    export DEV_USER=amp
  fi
fi

COMPOSE_ARGS=(--file "${ROOT_DIR}/docker/compose.yaml")
if [[ "${AMP_GUI:-0}" == "1" ]]; then
  COMPOSE_ARGS+=(--file "${ROOT_DIR}/docker/compose.gui.yaml")
fi
