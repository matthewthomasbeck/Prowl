import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    config = os.path.join(
        get_package_share_directory('prowl_ld19_driver'), 'config', 'ld19_driver.yaml')
    return LaunchDescription([
        Node(
            package='prowl_ld19_driver',
            executable='ld19_node',
            name='ld19_driver',
            parameters=[config],
            output='screen',
        ),
    ])
