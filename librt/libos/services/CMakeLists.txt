
add_library(libos-services STATIC
        file.c
        fileview.c
        mount.c
        net.c
        path.c
        process.c
        shared_objects.c
)
target_compile_definitions(libos-services PRIVATE -D__OSLIB_C_IMPLEMENTATION)
target_include_directories(libos-services PRIVATE
        ../../libc/include
        ../include
)
target_link_libraries(libos-services PUBLIC gracht_static libds libddk)
