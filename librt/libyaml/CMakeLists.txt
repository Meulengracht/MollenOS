# Project setup
project (vali-lib-yaml)
enable_language (C)

add_library(libyaml STATIC
        api.c
        parser.c
        reader.c
        scanner.c
)
target_include_directories(libyaml PUBLIC include)
target_link_libraries(libyaml libc)
