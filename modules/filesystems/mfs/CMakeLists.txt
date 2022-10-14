# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}/protocols
)

add_filesystem_target(mfs
    directory_operations.c
    file_operations.c
    main.c
    records.c
    utilities.c
)
deploy_file_to_initrd(mfs "modules" mfs.yaml)
