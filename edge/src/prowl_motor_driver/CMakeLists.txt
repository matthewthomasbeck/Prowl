cmake_minimum_required(VERSION 3.18)
project(prowl_motor_driver)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(ament_index_cpp REQUIRED)

# pigpio daemon client library (Debian/Ubuntu package: libpigpiod-if-dev)
find_library(PIGPIOD_IF2_LIBRARY pigpiod_if2 REQUIRED)
find_path(PIGPIOD_IF2_INCLUDE_DIR pigpiod_if2.h REQUIRED)

add_executable(motor_driver_node
  src/motor_driver_node.cpp
  src/motor_channel.cpp
)
target_compile_features(motor_driver_node PRIVATE cxx_std_17)
target_include_directories(motor_driver_node PRIVATE include ${PIGPIOD_IF2_INCLUDE_DIR})
target_link_libraries(motor_driver_node PRIVATE
  rclcpp::rclcpp
  ${geometry_msgs_TARGETS}
  ${std_msgs_TARGETS}
  ${PIGPIOD_IF2_LIBRARY}
)

# Terminal control panel (ncurses) that runs the driver and drives it over ROS2.
set(CURSES_NEED_NCURSES TRUE)
find_package(Curses REQUIRED)

add_executable(motor_panel src/motor_panel.cpp)
target_compile_features(motor_panel PRIVATE cxx_std_17)
target_include_directories(motor_panel PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(motor_panel PRIVATE
  rclcpp::rclcpp
  ament_index_cpp::ament_index_cpp
  ${std_msgs_TARGETS}
  ${CURSES_LIBRARIES}
)

install(TARGETS motor_driver_node motor_panel DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

ament_package()
