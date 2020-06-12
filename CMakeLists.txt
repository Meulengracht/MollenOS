cmake_minimum_required (VERSION 3.8.2)

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
option (VALI_INIT_APP "The application to execute for system initialization" "vioarr.app")

set (VALI_ARCH "i386" CACHE STRING "Platform architecture to build Vali for")
set_property (CACHE VALI_ARCH PROPERTY STRINGS i386 amd64)
set (ENV{VALI_ARCH} ${VALI_ARCH})

# Enable the system image switch
set (VALI_BUILD ON)
set (VALI_PROJECTS "")

# Initialize project
include (cmake/SetupEnvironment.cmake)
project (ValiOS)

# Set paths
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set (VALI_PATH_DEPLOY ${CMAKE_BINARY_DIR}/deploy)
set (VALI_PATH_INITRD ${CMAKE_BINARY_DIR}/initrd)

# Create neccessary directories
file(MAKE_DIRECTORY ${VALI_PATH_DEPLOY})
file(MAKE_DIRECTORY ${VALI_PATH_INITRD})

# Run early build initialization that require host compiler
include (ExternalProject)
ExternalProject_Add(tools
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/tools
    CMAKE_CACHE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR})

# Configure projects that are specified
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${VALI_ARCH}PlatformConfig.cmake")
    message (FATAL_ERROR "The specified platform ${VALI_ARCH} has no configuration file")
endif ()

include (cmake/HandleFeatureFlags.cmake)
include (cmake/${VALI_ARCH}PlatformConfig.cmake)
foreach (proj in ${VALI_PROJECTS})
    message (STATUS "${proj}")
	if ("${proj}" MATCHES ".txt$")
		get_filename_component (proj_temp "${proj}" DIRECTORY)
	    add_subdirectory (${proj_temp})
	elseif ("${proj}" MATCHES ".cmake$")
        include (${proj})
	endif ()
endforeach (proj)

# Configure the kernel project
add_subdirectory (kernel)
