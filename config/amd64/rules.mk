# Makefile with rules and defines for the X86-64 platform
# 

# Export some flags used that are architecture specific for clang
build_target = target_amd64
arch_flags = -m64 -Damd64 -D__x86_64__ -D__STDC_FORMAT_MACROS_64 --target=amd64-pc-win32-itanium-coff -fdwarf-exceptions
PVS_PLATFORM=linux64

# -Xclang -flto-visibility-public-std makes sure to generate cxx-abi stuff without __imp_ 
# -std=c11 enables c11 support for C compilation 0;35
# -gdwarf enables dwarf debugging generation, should be used ... -fexceptions -fcxx-exceptions
disable_warnings = -Wno-address-of-packed-member -Wno-self-assign -Wno-unused-function
shared_flags = -U_WIN32 -fms-extensions -Wall -nostdlib -nostdinc -O3 -DMOLLENOS

###########################
# Hardware Configuration
###########################
config_flags += -D__OSCONFIG_HAS_MMIO
config_flags += -D__OSCONFIG_ACPI_SUPPORT
config_flags += -D__OSCONFIG_HAS_UART
config_flags += -D__OSCONFIG_HAS_DWCAS # Assume presence of CPUID_FEAT_ECX_CX16 in cpuid

# Sanitize for headless environment
ifndef VALI_HEADLESS
config_flags += -D__OSCONFIG_HAS_VIDEO
endif

# Kernel + Kernel environment compilation flags
ASFLAGS = -f bin -D$(VALI_ARCH) -D__$(VALI_ARCH)__ $(config_flags)
GCFLAGS = $(shared_flags) $(arch_flags) -ffreestanding $(disable_warnings) $(config_flags)
GCXXFLAGS = -std=c++17 -ffreestanding $(shared_flags) $(arch_flags) $(disable_warnings) $(config_flags)

# Shared link flags for everything. /debug:dwarf /ignore:4217
GLFLAGS = /nodefaultlib /machine:X64 /subsystem:native

# Userspace environment compilation flags
GUCFLAGS = $(shared_flags) $(arch_flags) $(disable_warnings)
GUCXXFLAGS = -std=c++11 $(shared_flags) $(arch_flags) $(disable_warnings)
