add_library(cjson STATIC
        cjson.c
        cjson_utils.c
)
target_link_libraries(cjson PUBLIC c libm)
target_include_directories(cjson PUBLIC include)
