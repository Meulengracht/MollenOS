add_library(processd-debugger
        debugger.c
        map_parser.c
        symbol_loader.c
)
target_include_directories(processd-debugger PRIVATE ../include)
target_compile_options(processd-debugger PRIVATE -nostdlib -Wno-address-of-packed-member)
target_link_libraries(processd-debugger PUBLIC libc)
