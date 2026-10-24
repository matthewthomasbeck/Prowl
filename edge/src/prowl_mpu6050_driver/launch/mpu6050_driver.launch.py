import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    config = os.path.join(
        get_package_share_directory('prowl_mpu6050_driver'), 'config', 'mpu6050_driver.yaml')
    return LaunchDescription([
        Node(
            package='prowl_mpu6050_driver',
            executable='mpu6050_node',
            name='mpu6050_driver',
            parameters=[config],
            output='screen',
        ),
    ])
