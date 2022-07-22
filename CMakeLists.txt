cmake_minimum_required (VERSION 3.13.4)

if (VALI_BUILD_UNIT_TESTS)
    project (vali-testing C)
    include(CMakeParseArguments)
    function (add_unit_test)
        cmake_parse_arguments(
                UNIT_TEST # prefix of output variables
                "" # list of names of the boolean arguments (only defined ones will be true)
                "FILE" # list of names of mono-valued arguments
                "INCLUDES;DEFINES;LIBS" # list of names of multi-valued arguments (output variables are lists)
                ${ARGN} # arguments of the function to parse, here we take the all original ones
        )
        string (REPLACE ".c" "" TEST_FILENAME ${UNIT_TEST_FILE})
        string (REPLACE "_test." "." TEST_FILE ${UNIT_TEST_FILE})
        list (REMOVE_ITEM UNIT_TEST_INCLUDES "${CMAKE_SOURCE_DIR}/librt/libc/include")
        add_executable (${TEST_FILENAME} ${CMAKE_CURRENT_SOURCE_DIR}/${TEST_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/${UNIT_TEST_FILE})
        target_include_directories(${TEST_FILENAME} PRIVATE ${UNIT_TEST_INCLUDES} ${CMAKE_SOURCE_DIR}/testing/include)
        target_compile_definitions(${TEST_FILENAME} PRIVATE ${UNIT_TEST_DEFINES})
        target_link_libraries (${TEST_FILENAME} PRIVATE cmocka ${UNIT_TEST_LIBS})
        add_test (NAME ${TEST_FILENAME} COMMAND ${TEST_FILENAME})
    endfunction ()

    # enable testing
    enable_testing()
    add_subdirectory(testing)

    # add all projects that support unit tests
    add_subdirectory (librt)
    add_subdirectory (services)

    return()
endif ()

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
set (VALI_PATH_DEPLOY_BOOT          ${CMAKE_BINARY_DIR}/deploy/hdd/boot)
set (VALI_PATH_DEPLOY_SYSTEM        ${CMAKE_BINARY_DIR}/deploy/hdd/system)
set (VALI_PATH_DEPLOY_SYSTEM_BIN    ${CMAKE_BINARY_DIR}/deploy/hdd/system/bin)
set (VALI_PATH_DEPLOY_SYSTEM_SRV    ${CMAKE_BINARY_DIR}/deploy/hdd/system/services)
set (VALI_PATH_DEPLOY_SYSTEM_MODS   ${CMAKE_BINARY_DIR}/deploy/hdd/system/modules)
set (VALI_PATH_DEPLOY_SHARED        ${CMAKE_BINARY_DIR}/deploy/hdd/shared)
set (VALI_PATH_DEPLOY_SHARED_BIN    ${CMAKE_BINARY_DIR}/deploy/hdd/shared/bin)
set (VALI_PATH_DEPLOY_SHARED_LIB    ${CMAKE_BINARY_DIR}/deploy/hdd/shared/lib)
set (VALI_PATH_DEPLOY_SHARED_INC    ${CMAKE_BINARY_DIR}/deploy/hdd/shared/include)
set (VALI_PATH_DEPLOY_SHARED_MAPS   ${CMAKE_BINARY_DIR}/deploy/hdd/shared/maps)
set (VALI_PATH_DEPLOY_SHARED_SHARE  ${CMAKE_BINARY_DIR}/deploy/hdd/shared/share)

# Setup utility macros for targets
macro (deploy_target_to_initrd RDTARGET)
    add_custom_command (TARGET ${RDTARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${RDTARGET}> ${VALI_PATH_DEPLOY_SYSTEM_BIN}
    )
endmacro ()

macro (deploy_file_to_initrd)
    if (${ARGC} GREATER_EQUAL 3)
        set (FILES_TO_DEPLOY "${ARGN}")
        list (POP_FRONT FILES_TO_DEPLOY) # target
        list (POP_FRONT FILES_TO_DEPLOY) # path

        get_target_property(D2RD_SOURCE_DIR ${ARGV0} SOURCE_DIR)
        foreach (RD_FILE ${FILES_TO_DEPLOY})
            add_custom_command (TARGET ${ARGV0} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy "${D2RD_SOURCE_DIR}/${RD_FILE}" "${VALI_PATH_DEPLOY_SYSTEM}/${ARGV1}"
            )
        endforeach ()
    else ()
        message (STATUS "usage: deploy_file_to_initrd <target> <path> files ...")
        message (FATAL_ERROR "Invalid number of arguments (${ARGC}) for deploy_file_to_initrd")
    endif ()
endmacro ()

# Setup tools
set (TOOL_RD ${CMAKE_BINARY_DIR}/tools/host/bin/mkvafs)
set (TOOL_DU ${CMAKE_BINARY_DIR}/tools/host/bin/osbuilder)
set (TOOL_LZ ${CMAKE_BINARY_DIR}/tools/host/bin/lzss)

# Create neccessary directories
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_HDD})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_BOOT})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SYSTEM})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SYSTEM_BIN})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SYSTEM_SRV})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SYSTEM_MODS})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SHARED})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SHARED_BIN})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SHARED_LIB})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SHARED_INC})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SHARED_MAPS})
file (MAKE_DIRECTORY ${VALI_PATH_DEPLOY_SHARED_SHARE})

# Read the project version
file(READ "VERSION" VALI_VERSION)

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
include_directories (${CMAKE_BINARY_DIR}/protocols)

# Configure the runtime libraries
add_subdirectory (librt)

# Configure the kernel project
add_subdirectory (kernel)

# Build services
add_subdirectory (services)

# Build modules
add_subdirectory (modules)

# Build tools
add_subdirectory (tools)

# Build userspace applications
# these are not the entire userspace that exists, most of these applications
# are just utility apps and small test programs that exists only to test a single
# feature or implementation. To run these you must have the entire userspace up and
# running as we have no terminal environment builtin.
add_subdirectory (apps)

# build unit tests
if (VALI_ENABLE_TESTING)
    include (ExternalProject)
    ExternalProject_Add(unit_tests
            SOURCE_DIR ${CMAKE_SOURCE_DIR}
            CMAKE_CACHE_ARGS
                -DVALI_BUILD_UNIT_TESTS:BOOL=ON
                -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/testing
    )
endif ()

# Installation and utility targets
add_custom_target (install_prepare
    COMMAND ${TOOL_RD} --arch ${VALI_ARCH} --compression aplib --out ${VALI_PATH_DEPLOY_BOOT}/initrd.mos ${VALI_PATH_DEPLOY_SYSTEM}
    COMMAND ${TOOL_LZ} c ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/kernel.mos ${VALI_PATH_DEPLOY_BOOT}/kernel.mos
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:phoenix> ${VALI_PATH_DEPLOY_BOOT}
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} --dest ${VALI_PATH_DEPLOY_SHARED_BIN} --pattern *.dll
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} --dest ${VALI_PATH_DEPLOY_SHARED_LIB} --pattern *.lib
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} --dest ${VALI_PATH_DEPLOY_SHARED_MAPS} --pattern *.map
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_dependencies(install_prepare imager rd lzss)

add_custom_target (install_img
    COMMAND ${TOOL_DU} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/models/vali-gpt.yaml --target img
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
add_dependencies(install_img install_prepare)

add_custom_target (install_vmdk
    COMMAND ${TOOL_DU} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/models/vali-gpt.yaml --target vmdk
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
add_dependencies(install_vmdk install_prepare)

add_custom_command(
    OUTPUT always_run.special disk_usb.img
    COMMAND ${CMAKE_COMMAND} -E copy disk.img disk_usb.img
)

add_custom_target (run_bochs
    COMMAND bochs -q -f ${CMAKE_CURRENT_SOURCE_DIR}/tools/setup.bochsrc
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS disk_usb.img
)

add_custom_target (run_qemu
        COMMAND qemu-system-x86_64
            -D ./log.txt -monitor stdio -smp 1 -m 4096
            -bios /usr/share/OVMF/OVMF_CODE.fd
            -drive if=virtio,file=disk.img,format=raw
            -net none -no-reboot
            -serial file:./qserial.txt
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        USES_TERMINAL
)
