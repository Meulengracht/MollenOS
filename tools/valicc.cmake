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
