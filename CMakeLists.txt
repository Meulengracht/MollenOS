cmake_minimum_required (VERSION 3.13.4)

# Enable the generation of compile commands for 
# use with tools like clangd and VSCode
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

if (__BUILD_UNIT_TESTS)
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

    if (${VALI_ARCH} STREQUAL "amd64")
        add_definitions (-m64 -DMOLLENOS -DTESTING)
    elseif (${VALI_ARCH} STREQUAL "i386")
        add_definitions (-m32 -DMOLLENOS -DTESTING)
    else ()
        message (FATAL_ERROR "Unsupported unit test architecture: ${VALI_ARCH}")
    endif ()

    # enable testing
    enable_testing()
    add_subdirectory(testing)

    # a lot of support libraries reside here, so add this first
    add_subdirectory(librt)

    # add all projects that support unit tests
    add_subdirectory(kernel)
    #add_subdirectory(services)

    return()
endif ()

# Enable the system image switch
set (VALI_BUILD ON)
set (VALI_BOOTSTRAP ON)

include (cmake/SetupEnvironment.cmake)
project (ValiOS)

# Setup options and defaults
option (VALI_ENABLE_KERNEL_TRACE "Enable debug tracing in the kernel" ON)
option (VALI_ENABLE_SMP "Enable support for multiple cores" ON)
option (VALI_ENABLE_DEBUG_CONSOLE "Enable the debug console on boot instead of splash" ON)
option (VALI_ENABLE_DEBUG_MODE "Enter the debug console mode after boot instead of normal system startup" ON)
option (VALI_ENABLE_NESTED_IRQS "Enable nested IRQs instead of traditional single IRQs" OFF)
option (VALI_ENABLE_SIGNALS "Enable support for thread signals" ON)
option (VALI_ENABLE_DRIVERS "Allow loading drivers, otherwise boot stops after loading services" ON)
option (VALI_ENABLE_EHCI "Enable support for USB 2.0" ON)
option (VALI_ENABLE_EHCI_64BIT "Enable support for USB 2.0 64 bit addressing mode" OFF)

option (VALI_BUILD_UNIT_TESTS "Build unit tests" OFF)

set (VALI_ARCH "amd64" CACHE STRING "Architecture to build Vali for")
set_property (CACHE VALI_ARCH PROPERTY STRINGS i386 amd64 aarch64)
set (ENV{VALI_ARCH} ${VALI_ARCH})

# This helps to specifiy the details of the platform we are building for
# This is not available for all architectures
set (VALI_PLATFORM "generic" CACHE STRING "Platform to build Vali for")

# This helps to specify the details of the platform variant we are building for
# and can be set to anything, its purpose is to differentiate between variants 
# of the same platform
set (VALI_PLATFORM_VARIANT "" CACHE STRING "Platform variant to build Vali for")

# Setup the valid strings for the platform property
set_property (CACHE VALI_PLATFORM PROPERTY STRINGS generic rpi)

set (ENV{VALI_PLATFORM} ${VALI_PLATFORM})

# Set paths and define the structure of the initial system image
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set (VALI_PATH_BOOT_ASSETS          ${CMAKE_BINARY_DIR}/boot-assets)
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
set (INITRAMFS_TARGETS)
macro (deploy_target_to_initrd RDTARGET)
    add_custom_command (TARGET ${RDTARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${RDTARGET}> ${VALI_PATH_DEPLOY_SYSTEM_BIN}
    )
    set (INITRAMFS_TARGETS ${INITRAMFS_TARGETS} RDTARGET)
endmacro ()
macro (deploy_service_to_initrd RDTARGET)
    add_custom_command (TARGET ${RDTARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${RDTARGET}> ${VALI_PATH_DEPLOY_SYSTEM_SRV}
    )
    set (INITRAMFS_TARGETS ${INITRAMFS_TARGETS} RDTARGET)
endmacro ()
macro (deploy_module_to_initrd RDTARGET)
    add_custom_command (TARGET ${RDTARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${RDTARGET}> ${VALI_PATH_DEPLOY_SYSTEM_MODS}
    )
    set (INITRAMFS_TARGETS ${INITRAMFS_TARGETS} RDTARGET)
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

# Find the llvm-objcopy tool, we need this
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Detect whether diskbuilder is available on the host so we do
# don't need to build it
find_program(
    TOOL_DU 
    NAMES "mkcdk"
)
if (NOT TOOL_DU)
    message (WARNING "mkcdk was not available on the host system")
    set (TOOL_DU)
else ()
    message (STATUS "Disk imager (mkcdk) was found!")
endif ()

# Setup tools
set (TOOL_RD ${CMAKE_BINARY_DIR}/tools/host/bin/mkvafs)
set (TOOL_LZ ${CMAKE_BINARY_DIR}/tools/host/bin/lzss)

# Create neccessary directories
file (MAKE_DIRECTORY ${VALI_PATH_BOOT_ASSETS})
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
    message (FATAL_ERROR "The specified platform ${VALI_ARCH} is not supported")
endif ()

include (cmake/HandleFeatureFlags.cmake)
include (cmake/${VALI_ARCH}PlatformConfig.cmake)

# Configure bootloader
add_subdirectory (boot)

# Generate all the neccessary files for the builds
add_subdirectory (protocols)
include_directories (${CMAKE_BINARY_DIR}/protocols)

# Configure the runtime libraries
add_subdirectory (librt/libfdt)
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
if (VALI_BUILD_UNIT_TESTS)
    include (ExternalProject)
    ExternalProject_Add(unit_tests
            SOURCE_DIR ${CMAKE_SOURCE_DIR}
            CMAKE_CACHE_ARGS
                -DVALI_ARCH:STRING=${VALI_ARCH}
                -D__BUILD_UNIT_TESTS:BOOL=ON
                -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/testing
    )
endif ()

# setup initramfs for boot folder
set(VALI_RAMDISK_ARCH "${VALI_ARCH}")
if (VALI_ARCH STREQUAL "aarch64")
    set(VALI_RAMDISK_ARCH "arm64")
endif ()
install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_INSTALL_PREFIX}/boot/payload)")
install(CODE "execute_process(COMMAND ${TOOL_RD} --arch ${VALI_RAMDISK_ARCH} --compression aplib --out ${CMAKE_INSTALL_PREFIX}/boot/payload/initrd.mos ${VALI_PATH_DEPLOY_SYSTEM})")

# For some platforms, we append a loader in front of the kernel image
# Installation and utility targets
if (VALI_PLATFORM STREQUAL "rpi")
    add_custom_target(rpi-loader-flat
        COMMAND "${CCROOT}/bin/llvm-objcopy" -O binary "${VALI_PATH_BOOT_ASSETS}/bin/rpi-loader.elf" "${CMAKE_CURRENT_BINARY_DIR}/bin/rpi-loader.bin"
        BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/bin/rpi-loader.bin"
        VERBATIM
    )
    add_dependencies(rpi-loader-flat loader)

    add_custom_command(
        OUTPUT "${VALI_PATH_BOOT_ASSETS}/kernel8.img"
        COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/tools/rpi/pack.py"
            --loader "${CMAKE_CURRENT_BINARY_DIR}/bin/rpi-loader.bin"
            --kernel "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/kernel.mos"
            --output "${VALI_PATH_BOOT_ASSETS}/kernel8.img"
        DEPENDS 
            "${CMAKE_CURRENT_BINARY_DIR}/bin/rpi-loader.bin"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/kernel.mos"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/rpi/pack.py" VERBATIM
    )
    add_custom_target(rpi-image DEPENDS "${VALI_PATH_BOOT_ASSETS}/kernel8.img")
    add_dependencies(rpi-image rpi-loader-flat vali-core)
    
    add_custom_target (install_prepare
        # Image targets may run without a full OS install. Stage the firmware
        # beside kernel8.img so the Pi recipe always has a complete boot root.
        COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --component rpi-firmware
        COMMAND ${TOOL_RD} --arch ${VALI_RAMDISK_ARCH} --compression aplib --out ${VALI_PATH_BOOT_ASSETS}/initrd.mos ${VALI_PATH_DEPLOY_SYSTEM}
        COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/tools/rpi/bundle.py"
            --phoenix $<TARGET_FILE:phoenix>
            --ramdisk "${VALI_PATH_BOOT_ASSETS}/initrd.mos"
            --output "${VALI_PATH_BOOT_ASSETS}/boot-payload.bin"
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} --dest ${VALI_PATH_DEPLOY_SHARED_BIN} --pattern *.dll
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} --dest ${VALI_PATH_DEPLOY_SHARED_LIB} --pattern *.lib
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} --dest ${VALI_PATH_DEPLOY_SHARED_MAPS} --pattern *.map
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS rpi-image phoenix
    )
else ()
    add_custom_target (install_prepare
        COMMAND ${TOOL_RD} --arch ${VALI_RAMDISK_ARCH} --compression aplib --out ${VALI_PATH_DEPLOY_BOOT}/initrd.mos ${VALI_PATH_DEPLOY_SYSTEM}
        COMMAND ${TOOL_LZ} c ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/kernel.mos ${VALI_PATH_DEPLOY_BOOT}/kernel.mos
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:phoenix> ${VALI_PATH_DEPLOY_BOOT}
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} --dest ${VALI_PATH_DEPLOY_SHARED_BIN} --pattern *.dll
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} --dest ${VALI_PATH_DEPLOY_SHARED_LIB} --pattern *.lib
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/utils.py --cp --source ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} --dest ${VALI_PATH_DEPLOY_SHARED_MAPS} --pattern *.map
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif ()

add_dependencies(install_prepare rd lzss)

if (VALI_PLATFORM STREQUAL "rpi")
    set(VALI_IMAGE_RECIPE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/models/vali-rpi-gpt.yaml)
elseif (VALI_ARCH STREQUAL "aarch64")
    set(VALI_IMAGE_RECIPE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/models/vali-arm64-gpt.yaml)
else ()
    set(VALI_IMAGE_RECIPE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/models/vali-gpt.yaml)
endif ()

add_custom_target (install_img
    COMMAND ${TOOL_DU} ${VALI_IMAGE_RECIPE} --target img
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
add_dependencies(install_img install_prepare)

add_custom_target (install_vmdk
    COMMAND ${TOOL_DU} ${VALI_IMAGE_RECIPE} --target vmdk
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
add_dependencies(install_vmdk install_prepare)

add_custom_target (mkimage
    COMMAND ${TOOL_DU} -o ./disk.img ${VALI_IMAGE_RECIPE}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
add_dependencies(mkimage install_prepare)
