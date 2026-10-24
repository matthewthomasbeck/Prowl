cmake_minimum_required(VERSION 3.18)
project(prowl_mpu6050_driver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)

# Registers, I2C access, conversions and gyro bias: plain C++, no ROS.
add_library(prowl_mpu6050 STATIC
  src/conversions.cpp
  src/gyro_bias.cpp
  src/i2c_device.cpp
  src/mpu6050.cpp
)
target_include_directories(prowl_mpu6050 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(mpu6050_node src/mpu6050_node.cpp)
target_link_libraries(mpu6050_node PRIVATE prowl_mpu6050 rclcpp::rclcpp ${sensor_msgs_TARGETS})

# Standalone terminal monitor that talks to the chip directly, without ROS.
set(CURSES_NEED_NCURSES TRUE)
find_package(Curses REQUIRED)
add_executable(mpu6050_tui tools/mpu6050_tui/mpu6050_tui.cpp)
target_include_directories(mpu6050_tui PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(mpu6050_tui PRIVATE prowl_mpu6050 ${CURSES_LIBRARIES})

install(TARGETS mpu6050_node mpu6050_tui DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  add_executable(mpu6050_test test/test_mpu6050.cpp)
  target_include_directories(mpu6050_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools/mpu6050_tui)
  target_link_libraries(mpu6050_test PRIVATE prowl_mpu6050)
  add_test(NAME mpu6050_test COMMAND mpu6050_test)
endif()

ament_package()
