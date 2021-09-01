# Merge static libraries together into a single static library file
macro (merge_libraries)
    # To produce a library we need at least one source file.
    # It is created by ADD_CUSTOM_COMMAND below and will helps 
    # also help to track dependencies.
    set (TARGET ${ARGV0})
    set (LIB_LIST "${ARGN}")
    list (REMOVE_AT LIB_LIST 0)

    set (SOURCE_FILE ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_depends.c)
    add_library (${TARGET} STATIC ${SOURCE_FILE})

    # Get a list of libraries and their full path
    foreach (LIB ${LIB_LIST})
        if (TARGET ${LIB})
            set(LIB_LOCATION ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/${LIB}.sll)
            
            set (STATIC_LIBS ${STATIC_LIBS} ${LIB_LOCATION})
            add_dependencies(${TARGET} ${LIB})
        else ()
            list (APPEND STATIC_LIBS ${LIB}.sll)
        endif ()
    endforeach ()
    
    # Make the generated dummy source file depended on all static input
    # libs. If input lib changes,the source file is touched
    # which causes the desired effect (relink).
    add_custom_command ( 
        OUTPUT  ${SOURCE_FILE}
        COMMAND ${CMAKE_COMMAND} -E touch ${SOURCE_FILE}
        DEPENDS ${STATIC_LIBS}
    )

    if (MSVC)
        # To merge libs, just pass them to lib.exe command line.
        set (LINKER_EXTRA_FLAGS "")
        foreach (LIB ${STATIC_LIBS})
            set (LINKER_EXTRA_FLAGS "${LINKER_EXTRA_FLAGS} ${LIB}")
        endforeach ()
        set_target_properties (${TARGET}
            PROPERTIES
                STATIC_LIBRARY_FLAGS "${LINKER_EXTRA_FLAGS}")
    else ()
        set (TARGET_LOCATION ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/${TARGET}.sll)  
        if (APPLE)
          # Use OSX's libtool to merge archives (ihandles universal 
          # binaries properly)
            add_custom_command (TARGET ${TARGET} POST_BUILD
                COMMAND rm ${TARGET_LOCATION}
                COMMAND /usr/bin/libtool -static -o ${TARGET_LOCATION} 
                ${STATIC_LIBS}
            )
        else ()
            # Generic Unix, Cygwin or MinGW. In post-build step, call
            # script, that extracts objects from archives with "ar x" 
            # and repacks them with "ar r"
            set (TARGET ${TARGET})
            configure_file (
                ../cmake/MergeLibraries.cmake.in
                ${CMAKE_CURRENT_BINARY_DIR}/merge_archives_${TARGET}.cmake 
                @ONLY
            )
            add_custom_command (TARGET ${TARGET} POST_BUILD
              COMMAND rm ${TARGET_LOCATION}
              COMMAND ${CMAKE_COMMAND} -P 
              ${CMAKE_CURRENT_BINARY_DIR}/merge_archives_${TARGET}.cmake
            )
        endif ()
    endif ()
endmacro ()
