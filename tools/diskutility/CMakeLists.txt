cmake_minimum_required(VERSION 3.13.4)
project (Vali-Diskutility)

add_custom_target (diskutility ALL
    COMMAND dotnet build osbuilder --output ${CMAKE_CURRENT_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
