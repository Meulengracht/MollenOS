# Makefile with rules and defines for the X86-32 platform
# 

# Export some flags used that are architecture specific for clang
build_target = target_i386
arch_flags = -m32 -Di386 -D__i386__ --target=i386-pc-win32-itanium-coff
PVS_PLATFORM=linux32

# -Xclang -flto-visibility-public-std makes sure to generate cxx-abi stuff without __imp_ 
# -std=c11 enables c11 support for C compilation 0;35
# -gdwarf enables dwarf debugging generation, should be used ... -fexceptions -fcxx-exceptions
disable_warnings = -Wno-address-of-packed-member -Wno-self-assign -Wno-unused-function
shared_flags = -U_WIN32 -fms-extensions -Wall -nostdlib -nostdinc -O3 -DMOLLENOS -Xclang -flto-visibility-public-std

###########################
# Hardware Configuration
###########################
config_flags += -D__OSCONFIG_HAS_MMIO
config_flags += -D__OSCONFIG_ACPI_SUPPORT

# Sanitize for headless environment
ifdef VALI_HEADLESS
config_flags += -D__OSCONFIG_HAS_UART
else
config_flags += -D__OSCONFIG_HAS_VIDEO
endif

# Kernel + Kernel environment compilation flags
ASFLAGS = -f bin -D$(VALI_ARCH) -D__$(VALI_ARCH)__ $(config_flags)
GCFLAGS = $(shared_flags) $(arch_flags) -ffreestanding $(disable_warnings) $(config_flags)
GCXXFLAGS = -std=c++17 -ffreestanding $(shared_flags) $(arch_flags) $(disable_warnings) $(config_flags)

# Shared link flags for everything. /debug:dwarf /ignore:4217
GLFLAGS = /nodefaultlib /machine:X86 /subsystem:native

# Userspace environment compilation flags
GUCFLAGS = $(shared_flags) $(arch_flags) $(disable_warnings)
GUCXXFLAGS = -std=c++17 $(shared_flags) $(arch_flags) $(disable_warnings)
GUCLIBRARIES = $(lib_path)/libcrt.lib $(lib_path)/libclang.lib $(lib_path)/libc.lib $(lib_path)/libunwind.lib $(lib_path)/libm.lib
GUCXXLIBRARIES = $(lib_path)/libcxx.lib $(lib_path)/libclang.lib $(lib_path)/libc.lib $(lib_path)/libunwind.lib $(lib_path)/libm.lib
