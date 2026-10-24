cmake_minimum_required(VERSION 3.18)
project(prowl_ld19_driver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)

# The LD19 protocol, parser, scan assembly and serial port: plain C++, no ROS.
add_library(prowl_ld19 STATIC
  src/protocol.cpp
  src/packet_parser.cpp
  src/scan_assembler.cpp
  src/scan_analysis.cpp
  src/serial_port.cpp
)
target_include_directories(prowl_ld19 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(prowl_ld19 PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Rotation -> sensor_msgs/LaserScan, kept out of the node so it can be tested.
add_library(prowl_ld19_ros STATIC src/laser_scan.cpp)
target_link_libraries(prowl_ld19_ros PUBLIC prowl_ld19 ${sensor_msgs_TARGETS})
set_target_properties(prowl_ld19_ros PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(ld19_node src/ld19_node.cpp)
target_link_libraries(ld19_node PRIVATE prowl_ld19_ros rclcpp::rclcpp)

# ncursesw, so braille and other UTF-8 text draw correctly.
set(CURSES_NEED_NCURSES TRUE)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)

# Standalone terminal viewer that reads the port directly, without ROS.
add_executable(ld19_tui tools/ld19_tui/ld19_tui.cpp)
target_include_directories(ld19_tui PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(ld19_tui PRIVATE prowl_ld19 ${CURSES_LIBRARIES})

install(TARGETS ld19_node ld19_tui DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  add_executable(ld19_test test/ld19_test.cpp)
  target_link_libraries(ld19_test PRIVATE prowl_ld19)
  add_test(NAME ld19_test COMMAND ld19_test)

  add_executable(laser_scan_test test/laser_scan_test.cpp)
  target_link_libraries(laser_scan_test PRIVATE prowl_ld19_ros)
  add_test(NAME laser_scan_test COMMAND laser_scan_test)
endif()

ament_package()
