add_module_target(valifs ""
        filter.c
        main.c
)
target_link_libraries(valifs PRIVATE fs-common vafs zstd-api libzstd_static)
deploy_file_to_initrd(valifs "modules" valifs.yaml)
