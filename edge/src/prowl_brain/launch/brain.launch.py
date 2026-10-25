# Starts the whole robot: the three drivers, then the brain.
#
#   docker/dev.sh ros2 launch prowl_brain brain.launch.py             # motors in dry run (default)
#   docker/dev.sh ros2 launch prowl_brain brain.launch.py live:=true  # motors really move
#
# All four start at once; the brain's startup (BrainNode::waitForDrivers) waits
# until each driver is actually publishing before it starts its state machine.
import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PythonExpression
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue


def config(package, name):
    return os.path.join(get_package_share_directory(package), 'config', name)


def generate_launch_description():
    live = LaunchConfiguration('live')
    # motor_driver.yaml says dry_run: false, so always override it: dry run unless live:=true.
    dry_run = ParameterValue(PythonExpression(["'", live, "'.lower() != 'true'"]), value_type=bool)

    return LaunchDescription([
        DeclareLaunchArgument('live', default_value='false',
                              description='true drives the real motors; anything else is a dry run'),
        Node(
            package='prowl_ld19_driver',
            executable='ld19_node',
            name='ld19_driver',
            parameters=[config('prowl_ld19_driver', 'ld19_driver.yaml')],
            output='screen',
        ),
        Node(
            package='prowl_mpu6050_driver',
            executable='mpu6050_node',
            name='mpu6050_driver',
            parameters=[config('prowl_mpu6050_driver', 'mpu6050_driver.yaml')],
            output='screen',
        ),
        Node(
            package='prowl_motor_driver',
            executable='motor_driver_node',
            name='motor_driver',
            parameters=[config('prowl_motor_driver', 'motor_driver.yaml'), {'dry_run': dry_run}],
            output='screen',
        ),
        Node(
            package='prowl_brain',
            executable='brain_node',
            name='brain',
            parameters=[config('prowl_brain', 'brain.yaml')],
            output='screen',
        ),
    ])
