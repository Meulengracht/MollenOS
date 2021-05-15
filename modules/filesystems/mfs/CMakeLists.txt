if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../../../cmake/SetupEnvironment.cmake)
    project(ValiModule_MFS)
endif ()

enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}/protocols
    ../../../librt/libgracht/include
    ../../../librt/libusb/include
    ../../../librt/libddk/include
    ../../../librt/libds/include
    ../../../librt/libc/include
    ../../../librt/include
)

add_filesystem_target(mfs
    directory_operations.c
    file_operations.c
    main.c
    records.c
    utilities.c
)
