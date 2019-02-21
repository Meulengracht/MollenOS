# Project setup
project (Vali-Kernel)
enable_language (C)

if (NOT DEFINED VALI_CROSS_TOOLS)
    message (FATAL_ERROR "You must define VALI_CROSS_TOOLS to point to a clang cross compiler")
endif ()

# Configure options
option (VALI_KERNEL_TRACE "Enable debug tracing in the kernel" ON)
option (VALI_SMP "Enable multicore support in the kernel" ON)
option (VALI_DEBUG_CONSOLE "Enable the debug console on boot instead of splash" ON)
option (VALI_DEBUG_MODE "Enter the debug console mode after boot instead of normal system startup" OFF)
option (VALI_RUN_CPP_TESTS "Enables user-mode testing programs for the c/c++ suite" ON)
option (VALI_RUN_KERNEL_TESTS "Enable kernel-mode testing suites of the operating system" OFF)

# Include the architecture
if (${VALI_PLATFORM} STREQUAL "i386" OR ${VALI_PLATFORM} STREQUAL "amd64")
    add_subdirectory (arch/x86)
else ()
	add_subdirectory (arch/${VALI_PLATFORM})
endif ()

# Configure include paths
include_directories (../librt/include ../librt/libc/include ../librt/libds/include ../librt/libddk/include ../librt/libacpi/include include)

# Configure source files
add_executable (vali-core

	# ACPI
	acpi/functions.c
	acpi/handlers.c
	acpi/hpet.c
	acpi/init.c
	acpi/interrupts.c
	acpi/osl.c
	acpi/scan.c
	acpi/tableparser.c

	# Components
	components/cpu.c
	components/domain.c
	components/ic.c
	components/memory.c

	# Modules
	modules/manager.c
	modules/module.c
	modules/ramdisk.c
	modules/signal.c

	# Output
	output/fonts/font6x12.c
	output/fonts/font8x16.c
	output/console.c
	output/log.c

	# Synchronization
	synchronization/atomicsection.c
	synchronization/criticalsection.c
	synchronization/semaphore_global.c
	synchronization/semaphore_slim.c

	# System calls
	system_calls/driver_calls.c
	system_calls/ipc_calls.c
	system_calls/memory_calls.c
	system_calls/module_calls.c
	system_calls/ossupport_calls.c
	system_calls/sharedobject_calls.c
	system_calls/synchonization_calls.c
	system_calls/system_calls.c
	system_calls/table.c
	system_calls/thread_calls.c

	# Tests
	tests/data_structures_tests.c
	tests/synchronization_tests.c
	tests/test_manager.c

	# Systems
	crc32.c
	debug.c
	deviceio.c
	garbagecollector.c
	handle.c
	heap.c
	interrupts.c
	interrupts_table.c
	machine.c
	memorybuffer.c
	memoryspace.c
	pipe.c
	scheduler.c
	threading.c
	time.c)
set_target_properties (
	vali-core
	PROPERTIES
		OUTPUT_NAME "syskrnl"
		SUFFIX ".mos")
target_link_libraries (vali-core ${VALI_PLATFORM}.lib)
