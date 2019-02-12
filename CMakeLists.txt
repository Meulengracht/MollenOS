cmake_minimum_required (VERSION 3.8)
project (Vali)

# Setup options and defaults
set (VALI_PLATFORM "i386" CACHE STRING "Platform architecture to build Vali for")
set_property (CACHE VALI_PLATFORM PROPERTY STRINGS i386 amd64)

# Enable the system image switch
set (VALI_BUILD ON)
set (VALI_PATH_DEPLOY ${CMAKE_BINARY_DIR}/deploy)
set (VALI_PATH_INITRD ${CMAKE_BINARY_DIR}/initrd)
set (VALI_PROJECTS "")

# Compile native tools needed to build the final image


# Configure compilation
include (config/${VALI_PLATFORM}/PlatformConfig.cmake)

# Configure projects that are specified
foreach (proj in ${VALI_PROJECTS})
    message (STATUS "${proj}")
	if ("${proj}" MATCHES ".txt$")
		get_filename_component (proj_temp "${proj}" DIRECTORY)
	    add_subdirectory (${proj_temp})
	elseif ("${proj}" MATCHES ".cmake$")
        include (${proj})
	endif ()
endforeach (proj)
