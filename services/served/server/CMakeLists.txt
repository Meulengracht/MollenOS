
add_library(served-server STATIC
        api.c
        install.c
        load.c
        setup.c
        state.c
)
set_target_properties(served-server
        PROPERTIES
        COMPILE_FLAGS "-Wno-address-of-packed-member"
)
target_include_directories(served-server PRIVATE ../include)
target_link_libraries(served-server PRIVATE gracht libusb libds libddk c librt)
target_link_libraries(served-server PUBLIC cjson)
