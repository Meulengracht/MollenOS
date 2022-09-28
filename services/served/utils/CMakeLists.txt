
add_library(served-utils STATIC
        copyfile.c
)
target_include_directories(served-utils PRIVATE ../include)
target_link_libraries(served-utils PRIVATE libos c)
