# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}/protocols
)

add_module_target(mfs ""
        bucket_map.c
        directory_operations.c
        file_operations.c
        main.c
        records.c
        utilities.c
)
target_link_libraries(mfs PRIVATE fs-common)
deploy_file_to_initrd(mfs "modules" mfs.yaml)
