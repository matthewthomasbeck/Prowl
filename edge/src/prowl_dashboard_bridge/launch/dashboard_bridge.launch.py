import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    config = os.path.join(
        get_package_share_directory('prowl_dashboard_bridge'), 'config', 'dashboard_bridge.yaml')
    return LaunchDescription([
        Node(
            package='prowl_dashboard_bridge',
            executable='dashboard_bridge',
            name='dashboard_bridge',
            parameters=[config],
            output='screen',
        ),
    ])
