# Configuration file for shared feature flags, specifies compile options
# and the projects needed for this target
if (NOT DEFINED VALI_BUILD)
    message (FATAL_ERROR "You must invoke the root cmake file, not the individual platform files")
endif ()

set (FEATURE_FLAGS "")

if (VALI_ENABLE_KERNEL_TRACE)
    set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_LOGGING_KTRACE")
endif ()

if (VALI_ENABLE_SMP)
    set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_ENABLE_MULTIPROCESSORS")
endif ()

if (VALI_ENABLE_DEBUG_CONSOLE)
    set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_DEBUGCONSOLE")
endif ()

if (VALI_ENABLE_DEBUG_MODE)
    set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_DEBUGMODE")
endif ()

if (VALI_ENABLE_NESTED_IRQS)
    set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_NESTED_INTERRUPTS")
endif ()

if (NOT VALI_ENABLE_SIGNALS)
    set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_DISABLE_SIGNALLING")
endif ()

if (NOT VALI_ENABLE_DRIVERS)
    set (FEATURE_FLAGS "${FEATURE_FLAGS}  -D__OSCONFIG_NODRIVERS")
endif ()

if (NOT VALI_ENABLE_EHCI)
    set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_DISABLE_EHCI")
endif ()

if (VALI_ENABLE_EHCI_64BIT)
    set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_EHCI_ALLOW_64BIT")
endif ()

if (VALI_RUN_KERNEL_TESTS)
    set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_TEST_KERNEL")
endif ()

if ("${VALI_INIT_APP}" STREQUAL "")
    message (FATAL_ERROR "VALI_INIT_APP must be set")
else ()
    set (FEATURE_FLAGS "${FEATURE_FLAGS} -D__OSCONFIG_INIT_APP=\\\"vioarr.app\\\"")
endif ()
