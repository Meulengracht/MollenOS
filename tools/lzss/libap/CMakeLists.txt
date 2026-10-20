# Reuse Vali's bounded portable AP32 decoder. The upstream apultra decoder is
# not used: unlike this implementation it can read past a truncated payload.
add_library(aplib_decoder OBJECT "${CMAKE_CURRENT_SOURCE_DIR}/../../../librt/libddk/initrd/depacks.c")
target_compile_definitions(aplib_decoder PRIVATE
    aP_depack_safe=aplib_depack_ap32 aP_get_orig_size=aplib_header_orig_size)

# apultra's encoder sources only; apultra.c is its standalone CLI.
add_library(libap STATIC
    ../aplib.c
    shrink.c
    matchfinder.c
    libdivsufsort/lib/divsufsort.c
    libdivsufsort/lib/sssort.c
    libdivsufsort/lib/trsort.c
    $<TARGET_OBJECTS:aplib_decoder>
)
target_compile_features(libap PRIVATE c_std_99)
target_include_directories(libap PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_include_directories(libap PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/libdivsufsort/include"
)
