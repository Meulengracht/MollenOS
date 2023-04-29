enable_language(C)

# Configure include paths
include_directories (
    ${CMAKE_BINARY_DIR}/protocols
    ${CMAKE_CURRENT_BINARY_DIR}
    ../../../librt/libgracht/include
    ../../../librt/libusb/include
    ../../../librt/libddk/include
    ../../../librt/libds/include
    ../../../librt/libc/include
)

add_module_target(e1000 ""
        e1000_80003es2lan.c
        e1000_80003es2lan.h
        e1000_82540.c
        e1000_82541.c
        e1000_82542.c
        e1000_82543.c
        e1000_82571.c
        e1000_api.c
        e1000_ich8lan.c
        e1000_mac.c
        e1000_manage.c
        e1000_nvm.c
        e1000_osdep.c
        e1000_phy.c
)
add_dependencies(e1000 contract_servers)
deploy_file_to_initrd(e1000 "modules" e1000.yaml)
