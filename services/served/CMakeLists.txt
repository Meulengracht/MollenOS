# add all the sub-libraries for served
add_subdirectory(libs)
add_subdirectory(server)

set (ADDITONAL_SOURCES
    ${CMAKE_BINARY_DIR}/protocols/chef_served_service_server.c
)
set_source_files_properties ( ${ADDITONAL_SOURCES} PROPERTIES GENERATED TRUE )

add_service_target(served ""
    ${ADDITONAL_SOURCES}
    main.c
)
target_link_libraries(served PRIVATE served-server)
target_include_directories(served PRIVATE include)
target_include_directories(served PRIVATE
        ${CMAKE_BINARY_DIR}/protocols
        ${CMAKE_CURRENT_BINARY_DIR}
        include
)
add_dependencies(served service_servers)
deploy_file_to_initrd(served "services" served.yaml)
