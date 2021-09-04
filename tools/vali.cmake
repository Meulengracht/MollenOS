##################################################
# Setup platform environment
##################################################
set(MOLLENOS ON)

set(CMAKE_EXECUTABLE_SUFFIX ".run")
set(CMAKE_STATIC_LIBRARY_PREFIX "")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".lib")
set(CMAKE_SHARED_LIBRARY_PREFIX "")  # lib
set(CMAKE_SHARED_LIBRARY_SUFFIX ".dll") # .so
set(CMAKE_IMPORT_LIBRARY_PREFIX "")
set(CMAKE_IMPORT_LIBRARY_SUFFIX ".lib")
set(CMAKE_LINK_LIBRARY_SUFFIX ".lib")
set(CMAKE_DL_LIBS "")
set(CMAKE_EXTRA_LINK_EXTENSIONS ".targets")

set(CMAKE_FIND_LIBRARY_PREFIXES "")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib;.sll")

set(CMAKE_LIBRARY_PATH_FLAG "-LIBPATH:")
set(CMAKE_LINK_LIBRARY_FLAG "")
set(CMAKE_LINK_DEF_FILE_FLAG "/def:")

if(DEFINED ENV{CROSS})
  if(NOT DEFINED VALI_ARCH AND NOT DEFINED ENV{VALI_ARCH})
      message(FATAL_ERROR "VALI_ARCH variable is not defined")
  endif()
  if(NOT DEFINED ENV{VALI_INCLUDES})
      message(STATUS "VALI_INCLUDES environmental variable is not defined")
  endif()
  if(NOT DEFINED ENV{VALI_LIBRARIES})
      message(STATUS "VALI_LIBRARIES environmental variable is not defined")
  endif()
  if(NOT DEFINED ENV{VALI_LFLAGS})
      message(STATUS "VALI_LFLAGS environmental variable is not defined")
  endif()

  set(CMAKE_CROSSCOMPILING ON)
  set(CMAKE_C_COMPILER "$ENV{CROSS}/bin/clang")
  set(CMAKE_CXX_COMPILER "$ENV{CROSS}/bin/clang++")
  set(CMAKE_LINKER "$ENV{CROSS}/bin/lld-link")
  set(CMAKE_AR "$ENV{CROSS}/bin/llvm-ar")
  set(CMAKE_RANLIB "$ENV{CROSS}/bin/llvm-ranlib")
  
  set(SYSTEM_INCLUDES "$ENV{VALI_INCLUDES}")
  set(LINK_LIBRARIES $ENV{VALI_LIBRARIES})
  set(LINK_FLAGS_BASE $ENV{VALI_LFLAGS})
else()
  message(FATAL_ERROR "Native cmake environment for Vali is not implemented")
endif()

##################################################
# LANGUAGE (C)
##################################################
set(CMAKE_C_COMPILE_OPTIONS_PIC "")
set(CMAKE_C_COMPILE_OPTIONS_PIE "")
set(CMAKE_SHARED_LIBRARY_C_FLAGS "-D_DLL")

set(CMAKE_C_COMPILE_OBJECT "<CMAKE_C_COMPILER> -o <OBJECT> <FLAGS> <DEFINES> <INCLUDES> ${SYSTEM_INCLUDES} -c <SOURCE>")
set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_LINKER> /lib <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
set(CMAKE_C_CREATE_SHARED_LIBRARY "<CMAKE_LINKER> /dll /entry:__CrtLibraryEntry ${LINK_FLAGS_BASE} <LINK_FLAGS> /out:<TARGET> <OBJECTS> <LINK_LIBRARIES>") 
set(CMAKE_C_CREATE_SHARED_MODULE ${CMAKE_C_CREATE_SHARED_LIBRARY})
set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_LINKER> ${LINK_FLAGS_BASE} <LINK_FLAGS> /entry:__CrtConsoleEntry /out:<TARGET> <OBJECTS> <LINK_LIBRARIES>")
set(CMAKE_C_STANDARD_LIBRARIES "crt.lib compiler-rt.lib c.lib m.lib")

##################################################
# LANGUAGE (C++)
##################################################
set(CMAKE_CXX_COMPILE_OPTIONS_PIC "")
set(CMAKE_CXX_COMPILE_OPTIONS_PIE "")
set(CMAKE_SHARED_LIBRARY_CXX_FLAGS "-D_DLL")

set(CMAKE_CXX_COMPILE_OBJECT "<CMAKE_CXX_COMPILER> -o <OBJECT> <FLAGS> <DEFINES> <INCLUDES> ${SYSTEM_INCLUDES} -c <SOURCE>")
set(CMAKE_CXX_CREATE_STATIC_LIBRARY "<CMAKE_LINKER> /lib <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
set(CMAKE_CXX_CREATE_SHARED_LIBRARY "<CMAKE_LINKER> /dll /entry:__CrtLibraryEntry ${LINK_FLAGS_BASE} <LINK_FLAGS> /out:<TARGET> <OBJECTS> <LINK_LIBRARIES>") 
set(CMAKE_CXX_CREATE_SHARED_MODULE ${CMAKE_CXX_CREATE_SHARED_LIBRARY})
set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_LINKER> ${LINK_FLAGS_BASE} <LINK_FLAGS> /entry:__CrtConsoleEntry /out:<TARGET> <OBJECTS> <LINK_LIBRARIES>")

if (DEFINED ENV{VALI_BOOTSTRAP})
  set(CMAKE_CXX_STANDARD_LIBRARIES ${CMAKE_C_STANDARD_LIBRARIES})
else()
  set(CMAKE_CXX_STANDARD_LIBRARIES "static_c++.lib static_c++abi.lib unwind.lib ${CMAKE_C_STANDARD_LIBRARIES}")
endif()
