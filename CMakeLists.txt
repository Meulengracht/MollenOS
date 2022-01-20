cmake_minimum_required (VERSION 3.13.4)

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

set (VALI_INIT_APP "vioarr.run" CACHE STRING "The application to execute for system initialization")

set (VALI_ARCH "amd64" CACHE STRING "Platform architecture to build Vali for")
set_property (CACHE VALI_ARCH PROPERTY STRINGS i386 amd64)
set (ENV{VALI_ARCH} ${VALI_ARCH})

# Set paths and define the structure of the initial system image
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set (VALI_PATH_DEPLOY               ${CMAKE_BINARY_DIR}/deploy)
set (VALI_PATH_DEPLOY_HDD           ${CMAKE_BINARY_DIR}/deploy/hdd)
set (VALI_PATH_DEPLOY_SYSTEM        ${CMAKE_BINARY_DIR}/deploy/hdd/system)
set (VALI_PATH_DEPLOY_SYSTEM_SRV    ${CMAKE_BINARY_DIR}/deploy/hdd/system/services)
set (VALI_PATH_DEPLOY_SYSTEM_MODS   ${CMAKE_BINARY_DIR}/deploy/hdd/system/modules)
set (VALI_PATH_DEPLOY_SHARED        ${CMAKE_BINARY_DIR}/deploy/hdd/shared)
set (VALI_PATH_DEPLOY_SHARED_BIN    ${CMAKE_BINARY_DIR}/deploy/hdd/shared/bin)
set (VALI_PATH_DEPLOY_SHARED_LIB    ${CMAKE_BINARY_DIR}/deploy/hdd/shared/lib)
set (VALI_PATH_DEPLOY_SHARED_INC    ${CMAKE_BINARY_DIR}/deploy/hdd/shared/include)
set (VALI_PATH_DEPLOY_SHARED_MAPS   ${CMAKE_BINARY_DIR}/deploy/hdd/shared/maps)
set (VALI_PATH_DEPLOY_SHARED_SHARE  ${CMAKE_BINARY_DIR}/deploy/hdd/shared/share)
set (VALI_PATH_INITRD               ${CMAKE_BINARY_DIR}/initrd)
set (VALI_PATH_APPLICATIONS         ${CMAKE_BINARY_DIR}/apps)
set (VALI_PATH_APPLICATIONS_SHARE   ${CMAKE_BINARY_DIR}/apps/share)

if (DEFINED ENV{VALI_APPLICATION_PATH})
    set (VALI_PATH_APPLICATIONS       $ENV{VALI_APPLICATION_PATH})
    set (VALI_PATH_APPLICATIONS_SHARE $ENV{VALI_APPLICATION_PATH}/share)
endif ()

# Setup utility macros for targets
macro (deploy_to_initrd)
    if ((${ARGC} EQUAL 1) OR (${ARGC} EQUAL 2))
        # ARGV0 = target
        # ARGV1 = mdrv on/off
        if (${ARGC} EQUAL 2 AND ${ARGV1})
            get_target_property(D2RD_SOURCE_DIR ${ARGV0} SOURCE_DIR)
            configure_file(${D2RD_SOURCE_DIR}/${ARGV0}.mdrv ${VALI_PATH_INITRD}/${ARGV0}.mdrv COPYONLY)
        endif ()

        add_custom_command (TARGET ${ARGV0} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${ARGV0}> ${VALI_PATH_INITRD}
        )
    else ()
        message (STATUS "usage: deploy_to_initrd <target> <copy mdrv ON/OFF>")
        message (FATAL_ERROR "Invalid number of arguments (${ARGC}) for deploy_to_initrd")
    endif ()
endmacro ()

# Setup tools
set (TOOL_RD ${CMAKE_BINARY_DIR}/tools-prefix/src/tools-build/bin/mkvafs)
set (TOOL_DU ${CMAKE_BINARY_DIR}/tools-prefix/src/tools-build/diskutility/osbuilder)
set (TOOL_LZ ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lzss)

# Create neccessary directories
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_HDD})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SYSTEM})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SYSTEM_SRV})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SYSTEM_MODS})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SHARED})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SHARED_BIN})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SHARED_LIB})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SHARED_INC})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SHARED_MAPS})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SHARED_SHARE})
file (MAKE_DIRECTORY ${VALI_PATH_INITRD})
file (MAKE_DIRECTORY ${VALI_PATH_APPLICATIONS})
file (MAKE_DIRECTORY ${VALI_PATH_APPLICATIONS_SHARE})

# Read the project version
file(READ "VERSION" VALI_VERSION)

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

# Generate all the neccessary files for the builds
add_subdirectory (protocols)

# Configure the runtime libraries
add_subdirectory (librt)

# Configure the kernel project
add_subdirectory (kernel)

# Build services
add_subdirectory (services)

# Build modules
add_subdirectory (modules)

# Build userspace applications
# these are not the entire userspace that exists, most of these applications
# are just utility apps and small test programs that exists only to test a single
# feature or implementation. To run these you must have the entire userspace up and
# running as we have no terminal environment builtin.
add_subdirectory (apps)

# Installation and utility targets
add_custom_target (install_applications
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${VALI_PATH_APPLICATIONS}/bin --dest ${VALI_PATH_DEPLOY_SHARED_BIN} --pattern *.app
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${VALI_PATH_APPLICATIONS}/bin --dest ${VALI_PATH_DEPLOY_SHARED_BIN} --pattern *.run
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${VALI_PATH_APPLICATIONS}/bin --dest ${VALI_PATH_DEPLOY_SHARED_BIN} --pattern *.dll
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${VALI_PATH_APPLICATIONS}/maps --dest ${VALI_PATH_DEPLOY_SHARED_MAPS} --pattern *.map
    #COMMAND ${CMAKE_COMMAND} -E copy_directory ${VALI_PATH_APPLICATIONS}/include/ ${VALI_PATH_DEPLOY_SHARED_INC}
    #COMMAND ${CMAKE_COMMAND} -E copy_directory ${VALI_PATH_APPLICATIONS}/lib/ ${VALI_PATH_DEPLOY_SHARED_LIB}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${VALI_PATH_APPLICATIONS}/share/ ${VALI_PATH_DEPLOY_SHARED_SHARE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target (install_prepare
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/resources/ ${VALI_PATH_DEPLOY_HDD}
    COMMAND ${TOOL_RD} --arch ${VALI_ARCH} --out ${CMAKE_BINARY_DIR}/initrd.mos ${VALI_PATH_INITRD}
    COMMAND ${TOOL_LZ} c ${CMAKE_BINARY_DIR}/initrd.mos ${VALI_PATH_DEPLOY_SYSTEM}/initrd.mos
    COMMAND ${TOOL_LZ} c ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/kernel.mos ${VALI_PATH_DEPLOY_SYSTEM}/kernel.mos
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:phoenix> ${VALI_PATH_DEPLOY_SYSTEM}
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} --dest ${VALI_PATH_DEPLOY_SHARED_BIN} --pattern *.dll
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} --dest ${VALI_PATH_DEPLOY_SHARED_LIB} --pattern *.lib
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} --dest ${VALI_PATH_DEPLOY_SHARED_MAPS} --pattern *.map
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_dependencies(install_prepare tools)

add_custom_target (install_img
    COMMAND ${TOOL_DU} --project ${CMAKE_CURRENT_SOURCE_DIR}/cmake/models/vali-gpt.yaml --target img
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
add_dependencies(install_img install_prepare install_applications)

add_custom_target (install_vmdk
    COMMAND ${TOOL_DU} --project ${CMAKE_CURRENT_SOURCE_DIR}/cmake/models/vali-gpt.yaml --target vmdk
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

add_custom_target (run_qemu
        COMMAND qemu-system-x86_64
            -D ./log.txt -monitor stdio -smp 1 -m 4096
            -bios /usr/share/OVMF/OVMF_CODE.fd
            -drive if=virtio,file=mollenos.img,format=raw
            -net none -no-reboot
            -serial file:./qserial.txt
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        USES_TERMINAL
)

add_custom_target(package
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --create-zip --zip-dir ${CMAKE_INSTALL_PREFIX}/vali-sdk --zip-out vali-sdk.zip
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --create-zip --zip-dir ${CMAKE_INSTALL_PREFIX}/vali-ddk --zip-out vali-ddk.zip
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/package_os
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${VALI_PATH_DEPLOY} --dest ${CMAKE_BINARY_DIR}/package_os --pattern *.sys
    COMMAND ${TOOL_RD} --arch ${VALI_ARCH} --out ${CMAKE_BINARY_DIR}/initrd.mos ${VALI_PATH_INITRD}
    COMMAND ${TOOL_LZ} c ${CMAKE_BINARY_DIR}/initrd.mos ${CMAKE_BINARY_DIR}/package_os/initrd.mos
    COMMAND ${TOOL_LZ} c ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/kernel.mos ${CMAKE_BINARY_DIR}/package_os/kernel.mos
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/resources ${CMAKE_BINARY_DIR}/package_os/
    COMMAND ${CMAKE_COMMAND} -E copy ${TOOL_DU} ${CMAKE_BINARY_DIR}/package_os/
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} --dest ${CMAKE_BINARY_DIR}/package_os --pattern DiscUtils*
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --create-zip --zip-dir ${CMAKE_BINARY_DIR}/package_os --zip-out vali.zip
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/package_os
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
