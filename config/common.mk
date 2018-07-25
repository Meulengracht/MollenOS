# Makefile with common rules and defines for the system
# Defines paths
#

# Sanitize for architecture
ifndef VALI_ARCH
$(error VALI_ARCH is not set)
endif

# Sanitize for toolchain
ifndef CROSS
$(error CROSS is not set)
endif

# Setup project tools
CC = $(CROSS)/bin/clang
CXX = $(CROSS)/bin/clang++
LD = $(CROSS)/bin/lld-link
LIB = $(CROSS)/bin/llvm-lib
AS = nasm

# Setup project paths
mkfile_path = $(abspath $(lastword $(MAKEFILE_LIST)))
config_path = $(abspath $(dir $(mkfile_path))/../)
workspace_path = $(abspath $(config_path)/../)
arch_path = $(abspath $(workspace_path)/kernel/arch/$(VALI_ARCH))
userspace_path = $(abspath $(workspace_path)/userspace)
include_path = $(userspace_path)/include
lib_path = $(userspace_path)/lib

# MollenOS Configuration, comment in or out for specific features
config_flags = 

# ACPI Configuration flags
#config_flags += -D__OSCONFIG_ACPIDEBUG
#config_flags += -D__OSCONFIG_ACPIDEBUGGER
#config_flags += -D__OSCONFIG_ACPIDEBUGMUTEXES
#config_flags += -D__OSCONFIG_REDUCEDHARDWARE

# OS Configuration
config_flags += -D__OSCONFIG_DISABLE_SIGNALLING # Kernel fault on all hardware signals
#config_flags += -D__OSCONFIG_LOGGING_KTRACE # Kernel Tracing
#config_flags += -D__OSCONFIG_ENABLE_MULTIPROCESSORS # Use all cores
#config_flags += -D__OSCONFIG_PROCESS_SINGLELOAD # No simuoultanous process loading
config_flags += -D__OSCONFIG_FULLDEBUGCONSOLE # Use a full debug console on height
#config_flags += -D__OSCONFIG_NODRIVERS # Don't load drivers, run it without for debug
#config_flags += -D__OSCONFIG_DISABLE_EHCI # Disable usb 2.0 support, run only in usb 1.1
#config_flags += -D__OSCONFIG_EHCI_ALLOW_64BIT # Allow the EHCI driver to utilize 64 bit dma buffers
#config_flags += -D__OSCONFIG_DISABLE_VIOARR # Disable auto starting the windowing system
#config_flags += -D__OSCONFIG_TEST_KERNEL # Enable testing mode of the operating system

# Include correct arch file
include $(dir $(mkfile_path))/$(VALI_ARCH)/rules.mk