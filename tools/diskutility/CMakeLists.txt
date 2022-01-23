cmake_minimum_required(VERSION 3.13.4)
project (diskbuilder)

add_custom_target (diskutility ALL
    COMMAND dotnet build osbuilder --output ${CMAKE_CURRENT_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/osbuilder DESTINATION bin)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ DESTINATION bin
        FILES_MATCHING PATTERN "*.dll")
