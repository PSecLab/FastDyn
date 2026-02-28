cmake_minimum_required(VERSION 3.10)
project(gz_wrapper CXX)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GZ_TRANSPORT REQUIRED gz-transport13)
pkg_check_modules(GZ_MSGS REQUIRED gz-msgs10)
pkg_check_modules(PROTOBUF REQUIRED protobuf)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${GZ_TRANSPORT_INCLUDE_DIRS}
    ${GZ_MSGS_INCLUDE_DIRS}
    ${PROTOBUF_INCLUDE_DIRS}
)

link_directories(
    ${GZ_TRANSPORT_LIBRARY_DIRS}
    ${GZ_MSGS_LIBRARY_DIRS}
    ${PROTOBUF_LIBRARY_DIRS}
)

add_library(gz_wrapper SHARED gazebo.cpp gazebo.h)

# Linking dependencies is optional here for static lib, but can be useful if you use target_link_libraries later
target_link_libraries(gz_wrapper
    ${GZ_TRANSPORT_LIBRARIES}
    ${GZ_MSGS_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
)

# Optional: set the output directory for the static library
set_target_properties(gz_wrapper PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# add_executable(services services.cpp)

# target_link_libraries(services
#     ${GZ_TRANSPORT_LIBRARIES}
#     ${GZ_MSGS_LIBRARIES}
#     ${PROTOBUF_LIBRARIES}
# )