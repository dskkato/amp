from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('video_device', default_value='/dev/video0'),
        DeclareLaunchArgument('image_width', default_value='640'),
        DeclareLaunchArgument('image_height', default_value='480'),
        DeclareLaunchArgument('frame_rate', default_value='30.0'),
        DeclareLaunchArgument('pixel_format', default_value='auto'),
        DeclareLaunchArgument('output_encoding', default_value='bgr8'),
        DeclareLaunchArgument('use_shmem_buffer', default_value='true'),
        Node(
            package='v4l2_camera',
            executable='v4l2_camera_node',
            name='v4l2_camera',
            arguments=[
                '--ros-args',
                '--log-level',
                'serialize_buffer_with_endpoint:=warn',
            ],
            output='screen',
            parameters=[{
                'video_device': LaunchConfiguration('video_device'),
                'image_width': LaunchConfiguration('image_width'),
                'image_height': LaunchConfiguration('image_height'),
                'frame_rate': LaunchConfiguration('frame_rate'),
                'pixel_format': LaunchConfiguration('pixel_format'),
                'output_encoding': LaunchConfiguration('output_encoding'),
                'use_shmem_buffer': LaunchConfiguration('use_shmem_buffer'),
            }],
        ),
    ])
