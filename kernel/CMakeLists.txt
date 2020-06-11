# Project setup
project (ValiKernel)
enable_language (C)

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
include_directories (
    ../librt/include
    ../librt/libc/include
    ../librt/libds/include
    ../librt/libddk/include
    ../librt/libgracht/include
    ../librt/libacpi/source/include
    arch/include
    include)

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

	# Memory
	memory/heap.c
	memory/memory_region.c
	memory/memory_space.c
    
	# Modules
	modules/manager.c
	modules/module.c
	modules/ramdisk.c

	# Output
	output/fonts/font6x12.c
	output/fonts/font8x16.c
	output/console.c
	output/log.c

	# Scheduling
	scheduling/futex.c
	scheduling/ipc_context.c
	scheduling/irq_spinlock.c
	scheduling/mutex.c
	scheduling/scheduler.c
	scheduling/semaphore.c
	scheduling/signal.c
	scheduling/threading.c

	# System calls
	system_calls/driver_calls.c
	system_calls/entry.c
	system_calls/memory_calls.c
	system_calls/module_calls.c
	system_calls/ossupport_calls.c
	system_calls/sharedobject_calls.c
	system_calls/synchonization_calls.c
	system_calls/system_calls.c
	system_calls/thread_calls.c

	# Tests
    tests/test_manager.c
    
	# Utils
	utils/crc32.c
	utils/dynamic_memory_pool.c
	utils/static_memory_pool.c

	# Systems
	debug.c
	deviceio.c
	handle.c
	handle_set.c
	interrupts.c
	interrupts_handlers.c
	interrupts_table.c
	machine.c
	time.c)
set_target_properties (
	vali-core
	PROPERTIES
		OUTPUT_NAME "syskrnl"
		SUFFIX ".mos")
target_link_libraries (vali-core vali-${VALI_PLATFORM}.lib)
