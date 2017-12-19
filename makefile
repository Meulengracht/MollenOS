# Primary make-file for building and preparing the OS image
# Required tools for building
# - nasm assembler
# - clang cross-compiler (requires cmake, svn, libelf)
# - monodevelop
# Required environmental stuff:
# - CROSS=/path/to/cross/home
#
export arch = i386
export CC = $(CROSS)/bin/clang
export CXX = $(CROSS)/bin/clang++
export LD = $(CROSS)/bin/lld-link
export LIB = $(CROSS)/bin/llvm-lib
export AS = nasm

# MollenOS Configuration, comment in or out for specific features
config_flags = 

# ACPI Configuration flags
#config_flags += -D__OSCONFIG_ACPIDEBUG
#config_flags += -D__OSCONFIG_ACPIDEBUGGER
#config_flags += -D__OSCONFIG_ACPIDEBUGMUTEXES
#config_flags += -D__OSCONFIG_REDUCEDHARDWARE

# OS Configuration
config_flags += -D__OSCONFIG_DISABLE_SIGNALLING # Kernel fault on all signals
#config_flags += -D__OSCONFIG_LOGGING_KTRACE # Kernel Tracing
#config_flags += -D__OSCONFIG_ENABLE_MULTIPROCESSORS # Use all cores
#config_flags += -D__OSCONFIG_PROCESS_SINGLELOAD # No simuoultanous process loading
config_flags += -D__OSCONFIG_FULLDEBUGCONSOLE # Use a full debug console on height
#config_flags += -D__OSCONFIG_NODRIVERS # Don't load drivers, run it without for debug
#config_flags += -D__OSCONFIG_DISABLE_EHCI # Disable usb 2.0 support, run only in usb 1.1
config_flags += -D__OSCONFIG_DISABLE_VIOARR # Disable auto starting the windowing system

#-std=c11 -gdwarf
disable_warnings = -Wno-address-of-packed-member -Wno-self-assign -Wno-unused-function
shared_flags = -target i386-pc-win32-itanium-coff -U_WIN32 -m32 -fms-extensions -Wall -ffreestanding -nostdlib -O3

export ASFLAGS = -f bin
export GCFLAGS = $(shared_flags) -DMOLLENOS -D$(arch) $(disable_warnings) $(config_flags)
export GCXXFLAGS = -std=c++17 $(shared_flags) -DMOLLENOS -D$(arch) $(disable_warnings) $(config_flags)
export GLFLAGS = /nodefaultlib /machine:X86 /subsystem:native /debug:dwarf /nopdb
export GLIBRARIES = ../lib/libcrt.lib ../lib/libclang.lib ../lib/libc.lib ../lib/libunwind.lib ../lib/libcxxabi.lib ../lib/libcxx.lib

.PHONY: all
all: build_tools gen_revision build_bootloader build_libraries build_kernel build_drivers build_userspace build_initrd

.PHONY: build_initrd
build_initrd:
	mkdir -p initrd
	cp librt/build/*.dll initrd/
	cp services/build/*.dll initrd/
	cp services/build/*.mdrv initrd/
	cp modules/build/*.dll initrd/
	cp modules/build/*.mdrv initrd/

.PHONY: build_tools
build_tools:
	$(MAKE) -C tools/lzss -f makefile
	$(MAKE) -C tools/rd -f makefile
	$(MAKE) -C tools/diskutility -f makefile
	$(MAKE) -C tools/revision -f makefile

.PHONY: gen_revision
gen_revision:
	./revision build clang
	cp revision.h kernel/include/revision.h

.PHONE: build_userspace
build_userspace:
	$(MAKE) -C userspace -f makefile

.PHONY: build_kernel
build_kernel:
	$(MAKE) -C kernel -f makefile

.PHONY: build_drivers
build_drivers:
	$(MAKE) -C services -f makefile
	$(MAKE) -C modules -f makefile

.PHONY: build_libraries
build_libraries:
	$(MAKE) -C librt -f makefile

.PHONY: build_bootloader
build_bootloader:
	$(MAKE) -C boot -f makefile

.PHONY: install_shared
install_shared:
	mkdir -p deploy/hdd
	mkdir -p deploy/hdd/shared
	mkdir -p deploy/hdd/system
	cp -a resources/system/. deploy/hdd/system/
	cp -a resources/shared/. deploy/hdd/shared/
	cp -a boot/build/. deploy/
	cp librt/build/*.dll deploy/hdd/system/
	./rd $(arch) initrd.mos
	./lzss c initrd.mos deploy/hdd/system/initrd.mos
	./lzss c kernel/build/syskrnl.mos deploy/hdd/system/syskrnl.mos

.PHONY: install_img
install_img: install_shared
	mono diskutility -auto -target img -scheme mbr
	cp mollenos.img mollenos_usb.img

.PHONY: install_vmdk
install_vmdk: install_shared
	mono diskutility -auto -target vmdk -scheme mbr

.PHONY: build_toolchain
build_toolchain:
	mkdir -p toolchain
	chmod +x ./tools/depends.sh
	chmod +x ./tools/checkout.sh
	chmod +x ./tools/build_clang.sh
	chmod +x ./tools/build_toolchain.sh
	bash ./tools/depends.sh
	bash ./tools/checkout.sh
	bash ./tools/build_clang.sh
	bash ./tools/build_toolchain.sh
	rm -rf toolchain

.PHONY: clean
clean:
	$(MAKE) -C boot -f makefile clean
	$(MAKE) -C librt -f makefile clean
	$(MAKE) -C services -f makefile clean
	$(MAKE) -C modules -f makefile clean
	$(MAKE) -C kernel -f makefile clean
	$(MAKE) -C userspace -f makefile clean
	$(MAKE) -C tools/lzss -f makefile clean
	$(MAKE) -C tools/rd -f makefile clean
	$(MAKE) -C tools/diskutility -f makefile clean
	$(MAKE) -C tools/revision -f makefile clean
	rm -f initrd.mos
	rm -rf deploy
	rm -rf initrd
	rm -f *.vmdk
	rm -f *.img
	rm -f *.lock
