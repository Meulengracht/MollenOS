if (NOT DEFINED VALI_BUILD)
    cmake_minimum_required(VERSION 3.8.2)
    include(../cmake/SetupEnvironment.cmake)
    project(ValiTestPrograms)
endif ()

macro (add_test_target)
    if (${ARGC} GREATER_EQUAL 3)
        set (SERVICE_SOURCES "${ARGN}")
        list (POP_FRONT SERVICE_SOURCES) # target
        list (POP_FRONT SERVICE_SOURCES) # compile flags

        add_executable(${ARGV0} ${SERVICE_SOURCES})
        set_target_properties (${ARGV0}
            PROPERTIES
                COMPILE_FLAGS "${ARGV1}"
                LINK_FLAGS "/lldmap /entry:__CrtConsoleEntry"
        )
        target_link_libraries (${ARGV0} libgracht libds libddk c librt)
    else ()
        message (FATAL_ERROR "Not enough parameters for add_test_target")
    endif ()
endmacro ()

# build os test applications
# these test application can only run in a bare environment - they use the kernel logging API
# to print information and this is not available when running vioarr (window manager)
add_subdirectory(wm_client_test)
add_subdirectory(wm_server_test)

# we do not have any CPP test programs because the CPP runtime is built by the userspace
# environment, where the full llvm/clang setup is built for the OS.
