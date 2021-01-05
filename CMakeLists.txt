cmake_minimum_required (VERSION 3.8.2)

# Enable the system image switch
set (VALI_BUILD ON)
set (VALI_BOOTSTRAP ON)

include (cmake/SetupEnvironment.cmake)
project (ValiOS)

# Setup options and defaults
option (VALI_ENABLE_KERNEL_TRACE "Enable debug tracing in the kernel" ON)
option (VALI_ENABLE_SMP "Enable multicore support in the kernel" ON)
option (VALI_ENABLE_DEBUG_CONSOLE "Enable the debug console on boot instead of splash" ON)
option (VALI_ENABLE_DEBUG_MODE "Enter the debug console mode after boot instead of normal system startup" ON)
option (VALI_ENABLE_NESTED_IRQS "Enter the debug console mode after boot instead of normal system startup" OFF)
option (VALI_ENABLE_SIGNALS "Enter the debug console mode after boot instead of normal system startup" ON)
option (VALI_ENABLE_DRIVERS "Enter the debug console mode after boot instead of normal system startup" ON)
option (VALI_ENABLE_EHCI "Enter the debug console mode after boot instead of normal system startup" ON)
option (VALI_ENABLE_EHCI_64BIT "Enter the debug console mode after boot instead of normal system startup" OFF)

option (VALI_RUN_KERNEL_TESTS "Enable kernel-mode testing suites of the operating system" OFF)

set (VALI_INIT_APP "vioarr.app" CACHE STRING "The application to execute for system initialization")

set (VALI_ARCH "i386" CACHE STRING "Platform architecture to build Vali for")
set_property (CACHE VALI_ARCH PROPERTY STRINGS i386 amd64)
set (ENV{VALI_ARCH} ${VALI_ARCH})

# Set paths
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set (VALI_PATH_DEPLOY             ${CMAKE_BINARY_DIR}/deploy)
set (VALI_PATH_DEPLOY_HDD         ${CMAKE_BINARY_DIR}/deploy/hdd)
set (VALI_PATH_DEPLOY_SYSTEM      ${CMAKE_BINARY_DIR}/deploy/hdd/system)
set (VALI_PATH_DEPLOY_SHARED      ${CMAKE_BINARY_DIR}/deploy/hdd/shared)
set (VALI_PATH_DEPLOY_SHARED_BIN  ${CMAKE_BINARY_DIR}/deploy/hdd/shared/bin)
set (VALI_PATH_DEPLOY_SHARED_LIB  ${CMAKE_BINARY_DIR}/deploy/hdd/shared/lib)
set (VALI_PATH_DEPLOY_SHARED_MAPS ${CMAKE_BINARY_DIR}/deploy/hdd/shared/maps)
set (VALI_PATH_INITRD             ${CMAKE_BINARY_DIR}/initrd)
set (VALI_PATH_APPLICATIONS       ${CMAKE_BINARY_DIR}/apps)

if (DEFINED ENV{VALI_APPLICATION_PATH})
    set (VALI_PATH_APPLICATIONS $ENV{VALI_APPLICATION_PATH})
endif ()

# Setup tools
set (TOOL_RD ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rd)
set (TOOL_LZ ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lzss)
set (TOOL_DU ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/diskutility)
set (TOOL_RV ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/revision)

# Create neccessary directories
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_HDD})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SYSTEM})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SHARED})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SHARED_BIN})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SHARED_LIB})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SHARED_MAPS})
file (MAKE_DIRECTORY ${VALI_PATH_INITRD})
file (MAKE_DIRECTORY ${VALI_PATH_APPLICATIONS})

# Run early build initialization that require host compiler
include (ExternalProject)
ExternalProject_Add(tools
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/tools
    CMAKE_CACHE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}
)

ExternalProject_Add(unit_tests
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/unit_tests
    CMAKE_CACHE_ARGS
    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}
)

# Configure projects that are specified
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${VALI_ARCH}PlatformConfig.cmake")
    message (FATAL_ERROR "The specified platform ${VALI_ARCH} has no configuration file")
endif ()

include (cmake/HandleFeatureFlags.cmake)
include (cmake/${VALI_ARCH}PlatformConfig.cmake)

# Configure bootloader
add_subdirectory (boot)

# Configure the runtime libraries
add_subdirectory (librt)

# Configure the kernel project
add_subdirectory (kernel)

# Build services
add_subdirectory (services)

# Build modules
add_subdirectory (modules)

# Build tests
add_subdirectory (tests)

# Installation and utility targets
add_custom_target (install_applications
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${VALI_PATH_APPLICATIONS}/bin --dest ${VALI_PATH_DEPLOY_SHARED_BIN} --pattern *.app
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${VALI_PATH_APPLICATIONS}/bin --dest ${VALI_PATH_DEPLOY_SHARED_BIN} --pattern *.dll
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${VALI_PATH_APPLICATIONS}/bin --dest ${VALI_PATH_DEPLOY_SHARED_MAPS} --pattern *.map
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target (install_prepare
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources/ ${VALI_PATH_DEPLOY_HDD}
    COMMAND ${TOOL_RD} --arch ${VALI_ARCH} --initrd ${VALI_PATH_INITRD} --out ${CMAKE_BINARY_DIR}/initrd.mos
    COMMAND ${TOOL_LZ} c ${CMAKE_BINARY_DIR}/initrd.mos ${VALI_PATH_DEPLOY_SYSTEM}/initrd.mos
    COMMAND ${TOOL_LZ} c ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/syskrnl.mos ${VALI_PATH_DEPLOY_SYSTEM}/syskrnl.mos
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} --dest ${VALI_PATH_DEPLOY_SHARED_BIN} --pattern *.dll
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} --dest ${VALI_PATH_DEPLOY_SHARED_LIB} --pattern *.lib
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} --dest ${VALI_PATH_DEPLOY_SHARED_MAPS} --pattern *.map
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_dependencies(install_prepare tools)

add_custom_target (install_img
    COMMAND mono ${TOOL_DU} -auto -target img -scheme mbr
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
add_dependencies(install_img install_prepare install_applications)

add_custom_target (install_vmdk
    COMMAND mono ${TOOL_DU} -auto -target vmdk -scheme mbr
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
add_dependencies(install_vmdk install_prepare install_applications)

add_custom_command(
    OUTPUT always_run.special mollenos_usb.img
    COMMAND ${CMAKE_COMMAND} -E copy mollenos.img mollenos_usb.img
)

add_custom_target (run_bochs
    COMMAND bochs -q -f ${CMAKE_CURRENT_SOURCE_DIR}/tools/setup.bochsrc
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS mollenos_usb.img
)

add_custom_target(package
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --create-zip --rev-path ${TOOL_RV} --zip-dir ${CMAKE_INSTALL_PREFIX}/vali-sdk --zip-out vali-sdk.zip
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --create-zip --rev-path ${TOOL_RV} --zip-dir ${CMAKE_INSTALL_PREFIX}/vali-ddk --zip-out vali-ddk.zip
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/package_os
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${VALI_PATH_DEPLOY} --dest ${CMAKE_BINARY_DIR}/package_os --pattern *.sys
    COMMAND ${TOOL_RD} --arch ${VALI_ARCH} --initrd ${VALI_PATH_INITRD} --out ${CMAKE_BINARY_DIR}/initrd.mos
    COMMAND ${TOOL_LZ} c ${CMAKE_BINARY_DIR}/initrd.mos ${CMAKE_BINARY_DIR}/package_os/initrd.mos
    COMMAND ${TOOL_LZ} c ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/syskrnl.mos ${CMAKE_BINARY_DIR}/package_os/syskrnl.mos
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/resources ${CMAKE_BINARY_DIR}/package_os/
    COMMAND ${CMAKE_COMMAND} -E copy ${TOOL_DU} ${CMAKE_BINARY_DIR}/package_os/
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} --dest ${CMAKE_BINARY_DIR}/package_os --pattern DiscUtils*
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --create-zip --rev-path ${TOOL_RV} --zip-dir ${CMAKE_BINARY_DIR}/package_os --zip-out vali.zip
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/package_os
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
