cmake_minimum_required(VERSION 3.18)
project(prowl_camera_driver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(PkgConfig REQUIRED)
# Ubuntu packages libcamera-dev and libturbojpeg0-dev, installed in docker/Dockerfile.
pkg_check_modules(LIBCAMERA REQUIRED IMPORTED_TARGET libcamera)
pkg_check_modules(TURBOJPEG REQUIRED IMPORTED_TARGET libturbojpeg)

# JPEG encoding and YUV -> RGB: plain C++, no ROS and no camera, so the tests can use it.
add_library(prowl_camera_jpeg STATIC src/jpeg.cpp)
target_include_directories(prowl_camera_jpeg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(prowl_camera_jpeg PUBLIC PkgConfig::TURBOJPEG)

# The camera through libcamera: plain C++, no ROS.
add_library(prowl_camera STATIC src/pi_camera.cpp)
target_include_directories(prowl_camera PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(prowl_camera PUBLIC PkgConfig::LIBCAMERA)

add_executable(camera_node src/camera_node.cpp)
target_link_libraries(camera_node PRIVATE
  prowl_camera
  prowl_camera_jpeg
  rclcpp::rclcpp
  ${sensor_msgs_TARGETS}
  ${std_srvs_TARGETS}
)

install(TARGETS camera_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  add_executable(camera_test test/camera_test.cpp)
  target_link_libraries(camera_test PRIVATE prowl_camera_jpeg)
  add_test(NAME camera_test COMMAND camera_test)
endif()

ament_package()
