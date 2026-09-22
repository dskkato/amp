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

    def __init__(self, topic: str, interval: float) -> None:
        super().__init__('shared_buffer_hz')
        self._topic = topic
        self._interval = interval
        self._count = 0
        self._shared_count = 0
        self._other_backend_count = 0
        self._last_time = time.monotonic()

        self._subscription = self.create_subscription(
            Image,
            topic,
            self._callback,
            qos_profile_sensor_data,
            acceptable_buffer_backends='shared_buffer',
        )
        self._timer = self.create_timer(interval, self._report)
        self.get_logger().info(
            f"measuring '{topic}', interval {interval:.3f} s, "
            "acceptable buffer backend: shared_buffer"
        )

    def _callback(self, message: Image) -> None:
        self._count += 1
        backend_type = getattr(message.data, 'backend_type', 'unknown')
        if backend_type == 'shared_buffer':
            self._shared_count += 1
        else:
            self._other_backend_count += 1

    def _report(self) -> None:
        now = time.monotonic()
        elapsed = now - self._last_time
        rate = self._count / elapsed if elapsed > 0.0 else 0.0
        self.get_logger().info(
            f'received: {self._count}, rate: {rate:.3f} Hz, '
            f'shared_buffer: {self._shared_count}, '
            f'other_backend: {self._other_backend_count}, '
            f'elapsed: {elapsed:.6f} s'
        )
        self._count = 0
        self._shared_count = 0
        self._other_backend_count = 0
        self._last_time = now


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--topic', default='/image_raw')
    parser.add_argument('--interval', type=float, default=1.0)
    cli_args = remove_ros_args(args=sys.argv)[1:]
    args = parser.parse_args(cli_args)
    if args.interval <= 0.0:
        parser.error('--interval must be positive')

    rclpy.init(args=sys.argv)
    node = SharedBufferHz(args.topic, args.interval)
    try:
        rclpy.spin(node)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    main()
