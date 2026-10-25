cmake_minimum_required(VERSION 3.18)
project(prowl_gps_driver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(diagnostic_msgs REQUIRED)

# NMEA parsing, the receiver's state and the serial port: plain C++, no ROS.
add_library(prowl_gps STATIC
  src/nmea.cpp
  src/receiver_state.cpp
  src/serial_port.cpp
)
target_include_directories(prowl_gps PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(prowl_gps PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Receiver state -> NavSatFix, TwistStamped and DiagnosticStatus, kept out of the node so it can be tested.
add_library(prowl_gps_ros STATIC src/navsat.cpp)
target_link_libraries(prowl_gps_ros PUBLIC prowl_gps
  ${sensor_msgs_TARGETS} ${geometry_msgs_TARGETS} ${diagnostic_msgs_TARGETS})
set_target_properties(prowl_gps_ros PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(gps_node src/gps_node.cpp)
target_link_libraries(gps_node PRIVATE prowl_gps_ros rclcpp::rclcpp)

install(TARGETS gps_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  add_executable(gps_test test/gps_test.cpp)
  target_link_libraries(gps_test PRIVATE prowl_gps)
  add_test(NAME gps_test COMMAND gps_test)

  add_executable(navsat_test test/navsat_test.cpp)
  target_link_libraries(navsat_test PRIVATE prowl_gps_ros)
  add_test(NAME navsat_test COMMAND navsat_test)
endif()

ament_package()
