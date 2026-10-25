import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    config = os.path.join(
        get_package_share_directory('prowl_camera_driver'), 'config', 'camera_driver.yaml')
    return LaunchDescription([
        Node(
            package='prowl_camera_driver',
            executable='camera_node',
            name='camera_driver',
            parameters=[config],
            output='screen',
        ),
    ])
