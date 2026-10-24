import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    config = os.path.join(
        get_package_share_directory('prowl_motor_driver'), 'config', 'motor_driver.yaml')
    return LaunchDescription([
        Node(
            package='prowl_motor_driver',
            executable='motor_driver_node',
            name='motor_driver',
            parameters=[config],
            output='screen',
        ),
    ])
