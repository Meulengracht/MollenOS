cmake_minimum_required (VERSION 3.8.2)

# Setup options and defaults
set (VALI_PLATFORM "i386" CACHE STRING "Platform architecture to build Vali for")
set_property (CACHE VALI_PLATFORM PROPERTY STRINGS i386 amd64)

# Enable the system image switch
set (VALI_BUILD ON)
set (VALI_PATH_DEPLOY ${CMAKE_BINARY_DIR}/deploy)
set (VALI_PATH_INITRD ${CMAKE_BINARY_DIR}/initrd)
set (VALI_PROJECTS "")

# Create neccessary directories
file(MAKE_DIRECTORY ${VALI_PATH_DEPLOY})
file(MAKE_DIRECTORY ${VALI_PATH_INITRD})

if(CMAKE_CROSSCOMPILING)
    set (VALI_WORKDIR "${CMAKE_BINARY_DIR}/host")
    file(MAKE_DIRECTORY ${VALI_WORKDIR})
else()
    set (VALI_WORKDIR "${CMAKE_BINARY_DIR}")
endif()

# Add native tools needed to build the final image
if (VALI_EARLY_BUILD)
    add_subdirectory (tools)
    return()
endif()

include(cmake/SetupEnvironment.cmake)
project(Vali)

# Run early build initialization that require host compiler
execute_process (COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}"
                        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                        -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
                        -DVALI_EARLY_BUILD=ON
                        -DVALI_CROSS_TOOLS=${VALI_CROSS_TOOLS}
                        -DVALI_PLATFORM=${VALI_PLATFORM}
                        ${CMAKE_SOURCE_DIR}
               WORKING_DIRECTORY "${VALI_WORKDIR}")

# Build just tools in the nested build. Don't specify a --config
# because we cannot know what config the developer will be using
# at this point. For non-multi-config generators, we've already
# specified CMAKE_BUILD_TYPE above.
execute_process (COMMAND ${CMAKE_COMMAND} --build . --target tools
                WORKING_DIRECTORY "${VALI_WORKDIR}")

# Add the tools project again so we can access it
add_subdirectory (tools)

# define the tools to be used in child projects
if (MSVC)
    set (REVISION_SUFFIX "Debug/revision.exe")
    set (RD_SUFFIX "Debug/rd.exe")
    set (LZSS_SUFFIX "Debug/lzss.exe")
elseif (CMAKE_GENERATOR STREQUAL "Xcode")
    set (REVISION_SUFFIX "Debug/revision")
    set (RD_SUFFIX "Debug/rd")
    set (LZSS_SUFFIX "Debug/lzss")
else()
    set (REVISION_SUFFIX "revision")
    set (RD_SUFFIX "rd")
    set (LZSS_SUFFIX "lzss")
endif()

set (REVISION_EXECUTABLE "${VALI_WORKDIR}/tools/${REVISION_SUFFIX}")
set (RD_EXECUTABLE "${VALI_WORKDIR}/tools/${RD_SUFFIX}")
set (LZSS_EXECUTABLE "${VALI_WORKDIR}/tools/${LZSS_SUFFIX}")

# Configure compilation
include (cmake/${VALI_PLATFORM}PlatformConfig.cmake)

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

# Configure the kernel project
add_subdirectory (kernel)
