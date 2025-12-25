cmake_minimum_required(VERSION 3.16)
project(boardrunner_sdk C)

set(CMAKE_C_STANDARD 11)

# Put outputs in the chosen build directory
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# External include dirs (like your Makefile -I...)
set(FASTDYN_INCLUDE_DIR "" CACHE PATH "Path to shared include dir containing device.h")
set(QEMU_INCLUDE_DIR "" CACHE PATH "Path to QEMU include dir")

if(NOT FASTDYN_INCLUDE_DIR)
  message(FATAL_ERROR "FASTDYN_INCLUDE_DIR is required (must contain device.h)")
endif()

if(NOT QEMU_INCLUDE_DIR)
  message(FATAL_ERROR "FASTDYN_INCLUDE_DIR is required (must contain device.h)")
endif()

# If you rely on glib (like your Makefile)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)

# ---------------------------
# 1) Build the shared VIO library once
# ---------------------------
file(GLOB_RECURSE VIO_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/boardrunner_vio/src/*.c"
)

add_library(boardrunner_vio SHARED ${VIO_SOURCES})
set_target_properties(boardrunner_vio PROPERTIES
  OUTPUT_NAME "boardrunner_vio"     # libboardrunner_vio.so
)

target_compile_options(boardrunner_vio PRIVATE -g -fPIC)

target_include_directories(boardrunner_vio PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}/include"
  ${GLIB_INCLUDE_DIRS}
  ${FASTDYN_INCLUDE_DIR}
)

if(QEMU_INCLUDE_DIR)
  target_include_directories(boardrunner_vio PUBLIC "${QEMU_INCLUDE_DIR}")
endif()

target_link_libraries(boardrunner_vio PRIVATE ${GLIB_LIBRARIES})
target_link_directories(boardrunner_vio PRIVATE ${GLIB_LIBRARY_DIRS})

# ---------------------------
# 2) Build ONE plugin: model.so
# ---------------------------
add_library(model MODULE "${CMAKE_CURRENT_LIST_DIR}/model/model.c")
set_target_properties(model PROPERTIES
  PREFIX ""                  # model.so not libmodel.so
  OUTPUT_NAME "model"
  BUILD_RPATH "\$ORIGIN"     # find libboardrunner_vio.so next to model.so
  INSTALL_RPATH "\$ORIGIN"
)

target_compile_options(model PRIVATE -g -fPIC)

target_include_directories(model PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/include"
  ${GLIB_INCLUDE_DIRS}
  ${FASTDYN_INCLUDE_DIR}
)

if(QEMU_INCLUDE_DIR)
  target_include_directories(model PRIVATE "${QEMU_INCLUDE_DIR}")
endif()

target_link_libraries(model PRIVATE boardrunner_vio ${GLIB_LIBRARIES})

# mkdir -p build
# cmake -S . -B build \
#   -DFASTDYN_INCLUDE_DIR=/scratch/Fastdyn/FastDyn/include \
#   -DQEMU_INCLUDE_DIR=/scratch/Fastdyn/FastDyn/qemu/include
# cmake --build build -j
