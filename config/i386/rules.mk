# Makefile with rules and defines for the X86-32 platform
# 

# Export some flags used that are architecture specific for clang
build_target = target_i386
arch_flags = -m32 -Di386 -D__i386__ --target=i386-pc-win32-itanium-coff 
link_flags = /machine:X86

# -Xclang -flto-visibility-public-std makes sure to generate cxx-abi stuff without __imp_ 
# -std=c11 enables c11 support for C compilation 0;35
# -gdwarf enables dwarf debugging generation, should be used ... -fexceptions -fcxx-exceptions
disable_warnings = -Wno-address-of-packed-member -Wno-self-assign -Wno-unused-function -Wno-atomic-alignment
shared_flags = -U_WIN32 -fms-extensions -Wall -nostdlib -nostdinc -O3 -DMOLLENOS -Xclang -flto-visibility-public-std

# MollenOS Configuration, comment in or out for specific features
config_flags = 

# Hardware Configuration
config_flags += -D__OSCONFIG_HAS_MMIO
config_flags += -D__OSCONFIG_HAS_VIDEO
#config_flags += -D__OSCONFIG_HAS_UART

# ACPI Configuration flags
config_flags += -D__OSCONFIG_ACPI_SUPPORT
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

# Kernel + Kernel environment compilation flags
ASFLAGS = -f bin -D$(VALI_ARCH) -D__$(VALI_ARCH)__
GCFLAGS = $(shared_flags) $(arch_flags) -ffreestanding $(disable_warnings) $(config_flags)
GCXXFLAGS = -std=c++17 -ffreestanding $(shared_flags) $(arch_flags) $(disable_warnings) $(config_flags)

# Shared link flags for everything. /debug:dwarf
GLFLAGS = /nodefaultlib $(link_flags) /subsystem:native

# Userspace environment compilation flags
GUCFLAGS = $(shared_flags) $(arch_flags) $(disable_warnings) $(config_flags)
GUCXXFLAGS = -std=c++17 $(shared_flags) $(arch_flags) $(disable_warnings) $(config_flags)
GUCLIBRARIES = ../lib/libcrt.lib ../lib/libclang.lib ../lib/libc.lib ../lib/libunwind.lib ../lib/libm.lib
GUCXXLIBRARIES = ../lib/libcxx.lib ../lib/libclang.lib ../lib/libc.lib ../lib/libunwind.lib ../lib/libm.lib
