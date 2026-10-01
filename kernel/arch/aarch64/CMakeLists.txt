# CMake configuration for the AArch64 architecture-specific code.
if ("${VALI_PLATFORM}" STREQUAL "rpi")
    add_subdirectory(rpi)
    include_directories(rpi)
endif ()

#add_kernel_library(vali-aarch64 ""
#    common/...
#)
