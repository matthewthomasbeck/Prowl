# Starts what the bridge's test page (http://<pi>:8000/) needs: the LD19 driver, the motor driver and
# the bridge. No brain, so nothing else publishes /cmd_vel.
#
#   docker/dev.sh ros2 launch prowl_dashboard_bridge test_page.launch.py             # motors in dry run (default)
#   docker/dev.sh ros2 launch prowl_dashboard_bridge test_page.launch.py live:=true  # motors really move
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
            package='prowl_motor_driver',
            executable='motor_driver_node',
            name='motor_driver',
            parameters=[config('prowl_motor_driver', 'motor_driver.yaml'), {'dry_run': dry_run}],
            output='screen',
        ),
        Node(
            package='prowl_dashboard_bridge',
            executable='dashboard_bridge',
            name='dashboard_bridge',
            parameters=[config('prowl_dashboard_bridge', 'dashboard_bridge.yaml')],
            output='screen',
        ),
    ])
