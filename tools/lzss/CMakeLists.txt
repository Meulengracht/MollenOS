cmake_minimum_required(VERSION 3.14.3)
project(lzap C)

add_executable (lzss main.c)
add_subdirectory(libap)
target_link_libraries(lzss PRIVATE libap)
install(TARGETS lzss DESTINATION bin)
