##################################################
# Setup platform environment
##################################################
set(MOLLENOS ON)

# No -fPIC on Vali
set(CMAKE_C_COMPILE_OPTIONS_PIC "")
set(CMAKE_CXX_COMPILE_OPTIONS_PIC "")

set(CMAKE_C_COMPILE_OPTIONS_PIE "") 
set(CMAKE_CXX_COMPILE_OPTIONS_PIE "")

set(CMAKE_SHARED_LIBRARY_C_FLAGS "")
set(CMAKE_SHARED_LIBRARY_CXX_FLAGS "")

# Setup cmake rules, we do not care for default stuff
set(CMAKE_C_CREATE_SHARED_LIBRARY "<CMAKE_LINKER> <LINK_FLAGS> /dll $ENV{LIBRARIES}/libcrt.lib <OBJECTS> /out:<TARGET> /entry:__CrtLibraryEntry <LINK_LIBRARIES>") 
set(CMAKE_CXX_CREATE_SHARED_LIBRARY "<CMAKE_LINKER> <LINK_FLAGS> /dll $ENV{LIBRARIES}/libcxx.lib <OBJECTS> /out:<TARGET> /entry:__CrtLibraryEntry <LINK_LIBRARIES>") 

set(CMAKE_C_CREATE_SHARED_MODULE ${CMAKE_C_CREATE_SHARED_LIBRARY})
set(CMAKE_CXX_CREATE_SHARED_MODULE ${CMAKE_CXX_CREATE_SHARED_LIBRARY})

set(CMAKE_C_LINK_EXECUTABLE "<CMAKE_LINKER> <LINK_FLAGS> $ENV{LIBRARIES}/libcrt.lib <OBJECTS> /out:<TARGET> /entry:__CrtConsoleEntry <LINK_LIBRARIES>")
set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_LINKER> <LINK_FLAGS> $ENV{LIBRARIES}/libcxx.lib <OBJECTS> /out:<TARGET> /entry:__CrtConsoleEntry <LINK_LIBRARIES>")
