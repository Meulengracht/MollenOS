if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../../cmake/SetupEnvironment.cmake)
    project(ValiStorageModules)
endif ()

# build storage modules
add_subdirectory(ahci)
add_subdirectory(msd)
