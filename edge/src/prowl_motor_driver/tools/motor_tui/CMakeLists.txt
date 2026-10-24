# Standalone build (no ROS2): a terminal test panel for MotorChannel.
cmake_minimum_required(VERSION 3.18)
project(motor_tui CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic)

set(CURSES_NEED_NCURSES TRUE)
find_package(Curses REQUIRED)
find_library(PIGPIOD_IF2_LIBRARY pigpiod_if2 REQUIRED)
find_path(PIGPIOD_IF2_INCLUDE_DIR pigpiod_if2.h REQUIRED)

set(DRIVER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(motor_tui
  motor_tui.cpp
  ${DRIVER_DIR}/src/motor_channel.cpp
)
target_include_directories(motor_tui PRIVATE
  ${DRIVER_DIR}/include
  ${CURSES_INCLUDE_DIRS}
  ${PIGPIOD_IF2_INCLUDE_DIR}
)
target_link_libraries(motor_tui PRIVATE ${CURSES_LIBRARIES} ${PIGPIOD_IF2_LIBRARY})
