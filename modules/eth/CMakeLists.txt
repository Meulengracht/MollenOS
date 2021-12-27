if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.13.4)
    include(../../cmake/SetupEnvironment.cmake)
    project(vali-modules-eth)
endif ()

add_subdirectory(intel)
