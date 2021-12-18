# select proper subfolder architecture
if (${VALI_ARCH} STREQUAL "i386" OR ${VALI_ARCH} STREQUAL "amd64")
    add_subdirectory (x86)
else ()
    add_subdirectory (${VALI_ARCH})
endif ()
