cmake_minimum_required(VERSION 3.14.3)
project(vafs C)

option(VAFS_BUILD_TOOLS "Build fs tools for VaFs" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_subdirectory(libvafs)
add_subdirectory(filters)

if (VAFS_BUILD_TOOLS)
    add_subdirectory(tools)
endif(VAFS_BUILD_TOOLS)
