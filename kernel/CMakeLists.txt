if (__BUILD_UNIT_TESTS)
	# add all targets that support unit testing
	add_subdirectory (memory)
	return ()
endif ()

enable_language (C)

# Configure include paths
include_directories (
		${CMAKE_BINARY_DIR}
		${CMAKE_BINARY_DIR}/protocols
		include
		../boot/include
    	../librt/libgracht/include
    	../librt/libacpi/source/include
		../librt/libds/include
		../librt/libddk/include
		../librt/libos/include
		../librt/libc/include
)

# macro for adding subfolders as seperate projects
macro (add_kernel_library)
	if (${ARGC} GREATER_EQUAL 3)
		set (SERVICE_SOURCES "${ARGN}")
		list (POP_FRONT SERVICE_SOURCES) # target
		list (POP_FRONT SERVICE_SOURCES) # compile flags

		add_library(${ARGV0} STATIC ${SERVICE_SOURCES})
		target_compile_options(${ARGV0}
				PRIVATE $<$<COMPILE_LANGUAGE:C>:-mno-sse -DUNICODE -DLIBC_KERNEL -D__LIBDS_KERNEL__>
		)
		target_link_libraries(vali-core ${ARGV0})
	else ()
		message (FATAL_ERROR "Not enough parameters for add_kernel_library")
	endif ()
endmacro ()

# Configure source files
add_executable (vali-core
		../librt/libddk/io.c

		debug.c
		deviceio.c
		handle.c
		handle_set.c
		interrupts.c
		interrupts_handlers.c
		interrupts_table.c
		ipc.c
		machine.c
)

# add all subprojects after defining the vali-core target
# so dependencies gets properly added
add_subdirectory(acpi)
add_subdirectory(api)
add_subdirectory(arch)
add_subdirectory(components)
add_subdirectory(memory)
add_subdirectory(output)
add_subdirectory(sync)
add_subdirectory(threads)
add_subdirectory(utils)

set_target_properties (
	vali-core
	PROPERTIES
        LINK_FLAGS "-Xlinker -entry:kentry -Xlinker -fixed -Xlinker -base:\"0x100000\" -Xlinker -manifest:no"
		OUTPUT_NAME "kernel"
        SUFFIX ".mos"
)
target_compile_options(vali-core
	PRIVATE $<$<COMPILE_LANGUAGE:C>:-mno-sse -DUNICODE -DLIBC_KERNEL -D__LIBDS_KERNEL__>
)

# add additional link libraries from out of this tree
target_link_libraries (vali-core libacpi libdsk libk librt)
