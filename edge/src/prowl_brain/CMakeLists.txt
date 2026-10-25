cmake_minimum_required(VERSION 3.18)
project(prowl_brain CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rcl_interfaces REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(Threads REQUIRED)
# Ceres solver (Ubuntu package libceres-dev, installed in docker/Dockerfile).
find_package(Ceres REQUIRED)
# SDL2 for the map window on the robot's screen (libsdl2-dev, also in docker/Dockerfile).
find_package(SDL2 REQUIRED)

# The state machine, sensor mailbox, map and scan matcher: plain C++ and Ceres, no ROS.
add_library(prowl_brain_core STATIC
  src/sensor_buffer.cpp
  src/state_machine.cpp
  src/motion_predictor.cpp
  src/local_map.cpp
  src/ceres_scan_matcher.cpp
  src/matcher_thread.cpp
)
target_include_directories(prowl_brain_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(prowl_brain_core PUBLIC Ceres::ceres Threads::Threads)
set_target_properties(prowl_brain_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(brain_node src/main.cpp src/brain_node.cpp src/map_display.cpp)
target_link_libraries(brain_node PRIVATE
  prowl_brain_core
  SDL2::SDL2
  rclcpp::rclcpp
  ${rcl_interfaces_TARGETS}
  ${sensor_msgs_TARGETS}
  ${geometry_msgs_TARGETS}
  ${std_msgs_TARGETS}
)

install(TARGETS brain_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  add_executable(brain_core_test test/brain_core_test.cpp)
  target_link_libraries(brain_core_test PRIVATE prowl_brain_core)
  add_test(NAME brain_core_test COMMAND brain_core_test)
endif()

ament_package()
