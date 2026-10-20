# Sanitize expected environmental variables
set (CCROOT)
set (CCROOT_FOUND FALSE)
if(DEFINED ENV{VALICC})
    set (CCROOT "$ENV{VALICC}")
    set (CCROOT_FOUND TRUE)
elseif(DEFINED ENV{CROSS})
    set (CCROOT "$ENV{CROSS}")
    set (CCROOT_FOUND TRUE)
endif()

if(NOT ${CCROOT_FOUND})
    message(FATAL_ERROR "CROSS/VALICC environmental variable must point to a clang cross-compiler for Vali")
endif()

if(NOT DEFINED ENV{VALI_ARCH})
    if(VALI_ARCH)
        message(STATUS "VALI_ARCH is set to ${VALI_ARCH}.")
        set(ENV{VALI_ARCH} ${VALI_ARCH})
    else()
        message(STATUS "VALI_ARCH environmental variable was not set, defauling to amd64.")
        set(ENV{VALI_ARCH} amd64)
    endif()
endif()

# Setup environment stuff for cmake configuration
set(CMAKE_SYSTEM_NAME valicc)
set(CMAKE_SYSTEM_PROCESSOR $ENV{VALI_ARCH})
set(CMAKE_SYSTEM_VERSION 1)

# Tool paths describe host executables, even when targeting Vali.
set(VALI_HOST_EXECUTABLE_SUFFIX "")
if(CMAKE_HOST_WIN32)
    set(VALI_HOST_EXECUTABLE_SUFFIX ".exe")
endif()

set(CMAKE_C_COMPILER "${CCROOT}/bin/clang${VALI_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "${CCROOT}/bin/clang++${VALI_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_AR "${CCROOT}/bin/llvm-ar${VALI_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_RANLIB "${CCROOT}/bin/llvm-ranlib${VALI_HOST_EXECUTABLE_SUFFIX}" CACHE FILEPATH "")
set(VERBOSE 1)

set(LLVM_CMAKE_DIR "${CCROOT}/lib/cmake/llvm")
