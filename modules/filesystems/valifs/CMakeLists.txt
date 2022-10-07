add_filesystem_target(valifs
        filter.c
        main.c
)
target_link_libraries(valifs PRIVATE vafs zstd-api libzstd_static)
deploy_file_to_initrd(valifs "modules" valifs.yaml)
