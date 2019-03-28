##################################################
# Setup platform environment
##################################################
set(MOLLENOS ON)

set(CMAKE_EXECUTABLE_SUFFIX ".app")
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
set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")

set(CMAKE_LIBRARY_PATH_FLAG "-LIBPATH:")
set(CMAKE_LINK_LIBRARY_FLAG "")
set(CMAKE_LINK_DEF_FILE_FLAG "/def:")

##################################################
# LANGUAGE (C)
##################################################
set(CMAKE_C_COMPILE_OPTIONS_PIC "")
set(CMAKE_C_COMPILE_OPTIONS_PIE "")
set(CMAKE_SHARED_LIBRARY_C_FLAGS "")

set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_LINKER> /lib <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
set(CMAKE_C_CREATE_SHARED_LIBRARY "<CMAKE_LINKER> /dll /entry:__CrtLibraryEntry <LINK_FLAGS> /out:<TARGET> <OBJECTS> <LINK_LIBRARIES> libcrt.lib libunwind.lib libclang.lib libc.lib libm.lib") 
set(CMAKE_C_CREATE_SHARED_MODULE ${CMAKE_C_CREATE_SHARED_LIBRARY})
set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_LINKER> <LINK_FLAGS> /entry:__CrtConsoleEntry /out:<TARGET> <OBJECTS> <LINK_LIBRARIES> libcrt.lib libunwind.lib libclang.lib libc.lib libm.lib")

##################################################
# LANGUAGE (C++)
##################################################
set(CMAKE_CXX_COMPILE_OPTIONS_PIC "")
set(CMAKE_CXX_COMPILE_OPTIONS_PIE "")
set(CMAKE_SHARED_LIBRARY_CXX_FLAGS "")

set(CMAKE_CXX_CREATE_STATIC_LIBRARY "<CMAKE_LINKER> /lib <LINK_FLAGS> /out:<TARGET> <OBJECTS>")
set(CMAKE_CXX_CREATE_SHARED_LIBRARY "<CMAKE_LINKER> /dll /entry:__CrtLibraryEntry <LINK_FLAGS> /out:<TARGET> <OBJECTS> <LINK_LIBRARIES> libcxx.lib libunwind.lib libclang.lib libc.lib libm.lib") 
set(CMAKE_CXX_CREATE_SHARED_MODULE ${CMAKE_CXX_CREATE_SHARED_LIBRARY})
set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_LINKER> <LINK_FLAGS> /entry:__CrtConsoleEntry /out:<TARGET> <OBJECTS> <LINK_LIBRARIES> libcxx.lib libunwind.lib libclang.lib libc.lib libm.lib")
