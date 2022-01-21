# Contains all the filter targets in which
# we can import the filters from.

option(VAFS_BUILD_FILTER_APLIB "Build support for the aplib filter" ON)

if(VAFS_BUILD_FILTER_APLIB)
    add_subdirectory(aplib)
endif(VAFS_BUILD_FILTER_APLIB)
