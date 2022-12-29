add_library(processd-services
        discover.c
        parser.c
)
target_include_directories(processd-services PRIVATE ../include)
target_compile_options(processd-services PRIVATE -nostdlib -Wno-address-of-packed-member)
target_link_libraries(processd-services PUBLIC libyaml vafs libc)
