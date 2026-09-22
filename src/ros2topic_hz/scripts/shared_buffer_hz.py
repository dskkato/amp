#!/usr/bin/env python3

# Copyright 2026 Daisuke Kato
#
# Licensed under the Apache License, Version 2.0.

import argparse
import sys
import time

import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from rclpy.utilities import remove_ros_args
from sensor_msgs.msg import Image


class SharedBufferHz(Node):
    """Count Image arrivals while requiring the shared_buffer backend."""

    def __init__(self, topic: str, interval: float, acceptable_backend: str) -> None:
        super().__init__('ros2topic_shared_buffer_hz')
        self._topic = topic
        self._interval = interval
        self._acceptable_backend = acceptable_backend
        self._count = 0
        self._backend_counts: dict[str, int] = {}
        self._last_time = time.monotonic()

        self._subscription = self.create_subscription(
            Image,
            topic,
            self._callback,
            qos_profile_sensor_data,
            acceptable_buffer_backends=acceptable_backend,
        )
        self._timer = self.create_timer(interval, self._report)
        self.get_logger().info(
            f"measuring '{topic}', interval {interval:.3f} s, "
            f'acceptable buffer backend: {acceptable_backend}'
        )

    def _callback(self, message: Image) -> None:
        self._count += 1
        backend_type = getattr(message.data, 'backend_type', 'unknown')
        self._backend_counts[backend_type] = self._backend_counts.get(backend_type, 0) + 1

    def _report(self) -> None:
        now = time.monotonic()
        elapsed = now - self._last_time
        rate = self._count / elapsed if elapsed > 0.0 else 0.0
        backends = ', '.join(
            f'{backend}: {count}' for backend, count in sorted(self._backend_counts.items())
        ) or 'none'
        self.get_logger().info(
            f'received: {self._count}, rate: {rate:.3f} Hz, '
            f'backends: {backends}, elapsed: {elapsed:.6f} s'
        )
        self._count = 0
        self._backend_counts.clear()
        self._last_time = now


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--topic', default='/image_raw')
    parser.add_argument('--interval', type=float, default=1.0)
    parser.add_argument('--accept-buffer-backend', default='shared_buffer')
    cli_args = remove_ros_args(args=sys.argv)[1:]
    args = parser.parse_args(cli_args)
    if args.interval <= 0.0:
        parser.error('--interval must be positive')

    rclpy.init(args=sys.argv)
    node = SharedBufferHz(args.topic, args.interval, args.accept_buffer_backend)
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
