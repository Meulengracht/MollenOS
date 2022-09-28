add_library(cjson STATIC
        cjson.c
        cjson_utils.c
)
target_include_directories(cjson PUBLIC include)
