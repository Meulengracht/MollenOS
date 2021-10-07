# Sanitize expected environmental variables
if(NOT DEFINED ENV{CROSS})
    message(FATAL_ERROR "Please set the CROSS environmental variable to the path of the Vali Crosscompiler.")
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
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_C_COMPILER "$ENV{CROSS}/bin/clang" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "$ENV{CROSS}/bin/clang++" CACHE FILEPATH "")
set(CMAKE_AR "$ENV{CROSS}/bin/llvm-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "$ENV{CROSS}/bin/llvm-ranlib" CACHE FILEPATH "")
set(VERBOSE 1)

set(LLVM_CONFIG_PATH $ENV{CROSS}/bin/llvm-config)
