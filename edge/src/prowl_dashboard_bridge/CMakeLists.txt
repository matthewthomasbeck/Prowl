cmake_minimum_required(VERSION 3.18)
project(prowl_dashboard_bridge)

find_package(ament_cmake REQUIRED)

# The bridge is Node.js, so there is nothing to compile. Its npm packages (rclnodejs, ws) come from
# `npm ci` in this folder, which also builds rclnodejs's native addon and generates its message
# classes from the sourced ROS install.
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/node_modules/rclnodejs/build/Release/rclnodejs.node)
  message(FATAL_ERROR
    "prowl_dashboard_bridge: npm packages missing. Run, from the workspace:\n"
    "  docker/dev.sh bash -c 'cd src/prowl_dashboard_bridge && npm ci'\n"
    "(./prowl does this for you.)")
endif()

# The app goes under lib/ next to its launcher, so `ros2 run prowl_dashboard_bridge dashboard_bridge` works.
install(DIRECTORY bridge node_modules DESTINATION lib/${PROJECT_NAME}/app)
install(FILES package.json DESTINATION lib/${PROJECT_NAME}/app)
install(PROGRAMS scripts/dashboard_bridge DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config launch DESTINATION share/${PROJECT_NAME})

ament_package()
