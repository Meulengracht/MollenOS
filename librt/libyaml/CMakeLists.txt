# Project setup
project (vali-lib-yaml)
enable_language (C)

# Configure include paths
include_directories (
    ../libddk/include
    ../libds/include
    ../libc/include
    ./include
)

add_library(libyaml STATIC
        api.c
        parser.c
        reader.c
        scanner.c
)
