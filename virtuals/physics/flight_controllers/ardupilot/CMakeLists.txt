cmake_minimum_required(VERSION 3.10)
project(GCSReceiver C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    gcs_receiver.c
    ring_buffer.c
)

# Executable
add_executable(gcs_receiver ${SOURCES})

# Include directories
target_include_directories(gcs_receiver PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)