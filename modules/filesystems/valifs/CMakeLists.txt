add_filesystem_target(valifs
    main.c
)
target_link_libraries(valifs PRIVATE vafs)
deploy_file_to_initrd(valifs "modules" valifs.yaml)
