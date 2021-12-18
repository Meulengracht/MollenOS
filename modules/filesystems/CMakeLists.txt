if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.13.4)
    include(../../cmake/SetupEnvironment.cmake)
    project(ValiFilesystemsModules)
endif ()

# build filesystem modules
add_subdirectory(mfs)
