# rpi bootloader implementation
# consists of two parts:
#  - firmware
#  - our kernel bootstrapper
include(ExternalProject)

add_subdirectory(firmware)

# We unfortunately need to provide a different compiler environment
# so we need to include this as an external project
ExternalProject_Add(loader
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/loader
    BUILD_ALWAYS ON
    BUILD_BYPRODUCTS "${VALI_PATH_BOOT_ASSETS}/bin/rpi-loader.elf"
    CMAKE_ARGS
        -DCMAKE_ASM_COMPILER:PATH=${CMAKE_C_COMPILER}
        -DCMAKE_ASM_COMPILER_FORCED=ON
        -DCMAKE_C_COMPILER:PATH=${CMAKE_C_COMPILER}
        -DCMAKE_C_COMPILER_FORCED=ON
        -DCMAKE_INSTALL_PREFIX:PATH=${VALI_PATH_BOOT_ASSETS}
        -DVALI_ARCH=${VALI_ARCH}
        -DVALI_PLATFORM=${VALI_PLATFORM}
        -DVALI_PLATFORM_VARIANT=${VALI_PLATFORM_VARIANT}
)
