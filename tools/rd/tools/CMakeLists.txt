
add_executable(mkvafs
    mkvafs.c
)
target_include_directories(mkvafs PRIVATE ../libvafs/include)

target_link_libraries(mkvafs vafs)
if (UNIX)
target_link_libraries(mkvafs -lpthread)
endif (UNIX)

add_executable(unmkvafs
    unmkvafs.c
)
target_include_directories(unmkvafs PRIVATE ../libvafs/include)

target_link_libraries(unmkvafs vafs)
if (UNIX)
target_link_libraries(unmkvafs -lpthread)
endif (UNIX)
