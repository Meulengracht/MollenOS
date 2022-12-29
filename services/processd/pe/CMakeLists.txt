add_library(processd-pelib
        cache.c
        imports.c
        load_context.c
        loader.c
        mapper.c
        module.c
        parse.c
        resolver.c
        rtrelocs.c
        utilities.c
        verify.c
)
target_include_directories(processd-pelib PRIVATE ../include)
target_compile_options(processd-pelib PRIVATE -nostdlib -Wno-address-of-packed-member)
target_link_libraries(processd-pelib PUBLIC libc)
