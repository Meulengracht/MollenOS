add_library(chef STATIC
        package.c
)
target_link_libraries(chef PUBLIC vafs c)
target_include_directories(chef PUBLIC include)
