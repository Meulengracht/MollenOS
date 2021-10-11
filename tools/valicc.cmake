##################################################
# Setup platform environment
##################################################
set(MOLLENOS ON)
set(VALI ON)

# Sanitize expected environmental variables
if(NOT DEFINED ENV{VALI_ARCH})
    message(FATAL_ERROR "Vali-XClang: VALI_ARCH environmental variable is not defined")
endif()

set(CMAKE_EXECUTABLE_SUFFIX ".run")
set(CMAKE_STATIC_LIBRARY_PREFIX "")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".lib")
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".dll")
set(CMAKE_IMPORT_LIBRARY_PREFIX "")
set(CMAKE_IMPORT_LIBRARY_SUFFIX ".dll.lib")
set(CMAKE_EXTRA_LINK_EXTENSIONS ".targets")
set(CMAKE_LINK_LIBRARY_SUFFIX ".lib")
set(CMAKE_DL_LIBS "")

set(CMAKE_FIND_LIBRARY_PREFIXES "")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".dll.lib" ".lib")

set(CMAKE_LIBRARY_PATH_FLAG "-L")
set(CMAKE_LINK_LIBRARY_FLAG "-l")
set(CMAKE_LINK_DEF_FILE_FLAG "-Xlinker -def:")
set(LINK_LIBRARIES "")

# We need to preserve any flags that were passed in by the user. However, we
# can't append to CMAKE_C_FLAGS and friends directly, because toolchain files
# will be re-invoked on each reconfigure and therefore need to be idempotent.
# The assignments to the _INIT cache variables don't use FORCE, so they'll
# only be populated on the initial configure, and their values won't change
# afterward.
set(VALI_COMPILE_FLAGS "-fms-extensions")
if("$ENV{VALI_ARCH}" STREQUAL "i386")
    set(VALI_COMPILE_FLAGS "${VALI_COMPILE_FLAGS} --target=i386-uml-vali")
else()
    set(VALI_COMPILE_FLAGS "${VALI_COMPILE_FLAGS} --target=amd64-uml-vali")
endif()

##################################################
# LANGUAGE (ASM)
##################################################
set(CMAKE_ASM_COMPILE_OPTIONS_PIC "")
set(CMAKE_ASM_COMPILE_OPTIONS_PIE "")
set(_CMAKE_ASM_PIE_MAY_BE_SUPPORTED_BY_LINKER NO)
set(CMAKE_ASM_LINK_OPTIONS_PIE "")
set(CMAKE_ASM_LINK_OPTIONS_NO_PIE "")

##################################################
# LANGUAGE (C)
##################################################
set(CMAKE_C_COMPILE_OPTIONS_PIC "")
set(CMAKE_C_COMPILE_OPTIONS_PIE "")
set(_CMAKE_C_PIE_MAY_BE_SUPPORTED_BY_LINKER NO)
set(CMAKE_C_LINK_OPTIONS_PIE "")
set(CMAKE_C_LINK_OPTIONS_NO_PIE "")
set(CMAKE_SHARED_LIBRARY_C_FLAGS "-D_DLL")
set(CMAKE_C_STANDARD_LIBRARIES "")
string(APPEND CMAKE_C_FLAGS_INIT " ${VALI_COMPILE_FLAGS}")

set(CMAKE_C_CREATE_SHARED_LIBRARY "<CMAKE_C_COMPILER> <CMAKE_SHARED_LIBRARY_C_FLAGS> <LANGUAGE_COMPILE_FLAGS> <LINK_FLAGS> -Xlinker -implib:<TARGET_IMPLIB> -Xlinker -version:<TARGET_VERSION_MAJOR>.<TARGET_VERSION_MINOR> <CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>")
set(CMAKE_C_CREATE_SHARED_MODULE ${CMAKE_C_CREATE_SHARED_LIBRARY})
set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_C_COMPILER> <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> -Xlinker -version:<TARGET_VERSION_MAJOR>.<TARGET_VERSION_MINOR> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

##################################################
# LANGUAGE (C++)
##################################################
set(CMAKE_CXX_COMPILE_OPTIONS_PIC "")
set(CMAKE_CXX_COMPILE_OPTIONS_PIE "")
set(_CMAKE_CXX_PIE_MAY_BE_SUPPORTED_BY_LINKER NO)
set(CMAKE_CXX_LINK_OPTIONS_PIE "")
set(CMAKE_CXX_LINK_OPTIONS_NO_PIE "")
set(CMAKE_SHARED_LIBRARY_CXX_FLAGS "-D_DLL")
set(CMAKE_CXX_STANDARD_LIBRARIES "")
string(APPEND CMAKE_CXX_FLAGS_INIT " ${VALI_COMPILE_FLAGS}")

set(CMAKE_CXX_CREATE_SHARED_LIBRARY "<CMAKE_CXX_COMPILER> <CMAKE_SHARED_LIBRARY_CXX_FLAGS> <LANGUAGE_COMPILE_FLAGS> <LINK_FLAGS> -Xlinker -implib:<TARGET_IMPLIB> -Xlinker -version:<TARGET_VERSION_MAJOR>.<TARGET_VERSION_MINOR> <CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS> -o <TARGET> <OBJECTS> <LINK_LIBRARIES>")
set(CMAKE_CXX_CREATE_SHARED_MODULE ${CMAKE_C_CREATE_SHARED_LIBRARY})
set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> -Xlinker -version:<TARGET_VERSION_MAJOR>.<TARGET_VERSION_MINOR> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
