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
config_flags += -D__OSCONFIG_DISABLE_SIGNALLING # Kernel fault on all hardware signals
#config_flags += -D__OSCONFIG_LOGGING_KTRACE # Kernel Tracing
#config_flags += -D__OSCONFIG_ENABLE_MULTIPROCESSORS # Use all cores
#config_flags += -D__OSCONFIG_PROCESS_SINGLELOAD # No simuoultanous process loading
config_flags += -D__OSCONFIG_FULLDEBUGCONSOLE # Use a full debug console on height
#config_flags += -D__OSCONFIG_NODRIVERS # Don't load drivers, run it without for debug
#config_flags += -D__OSCONFIG_DISABLE_EHCI # Disable usb 2.0 support, run only in usb 1.1
#config_flags += -D__OSCONFIG_DISABLE_VIOARR # Disable auto starting the windowing system

# Before building llvm, one must export $(INCLUDES) to point at the include directory (full path)
# One must also specify $(CROSS) as per default
#cmake -G "Unix Makefiles" -DMOLLENOS=True -DCMAKE_CROSSCOMPILING=True -DLLVM_TABLEGEN=llvm-tblgen -DLLVM_DEFAULT_TARGET_TRIPLE=i386-pc-win32-itanium-coff -DCMAKE_C_FLAGS=' -U_WIN32 -m32 -fms-extensions -Wall -ffreestanding -nostdlib -nostdinc -O3 -DMOLLENOS -Di386 -I$(INCLUDES)/cxx -I$(INCLUDES)' -DCMAKE_CXX_FLAGS=' -U_WIN32 -m32 -fms-extensions -Wall -ffreestanding -nostdlib -nostdinc -O3 -DMOLLENOS -Di386 -I$(INCLUDES)/cxx -I$(INCLUDES)' -DLLVM_ENABLE_EH=True -DLLVM_ENABLE_RTTI=True -DCMAKE_BUILD_TYPE=Release -DLLVM_INCLUDE_TESTS=Off -DLLVM_INCLUDE_EXAMPLES=Off -DCMAKE_C_COMPILER=$CROSS/bin/clang -DCMAKE_CXX_COMPILER=$CROSS/bin/clang++ -DCMAKE_LINKER=$CROSS/bin/lld-link -DLLVM_USE_LINKER=$CROSS/bin/lld VERBOSE=1 ../llvm
#cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=../llvm/cmake/platforms/Vali.cmake ../llvm

# -Xclang -flto-visibility-public-std makes sure to generate cxx-abi stuff without __imp_ 
# -std=c11 enables c11 support for C compilation
# -gdwarf enables dwarf debugging generation, should be used ... -fexceptions -fcxx-exceptions
disable_warnings = -Wno-address-of-packed-member -Wno-self-assign -Wno-unused-function
shared_flags = -U_WIN32 -m32 -fms-extensions -Wall -nostdlib -nostdinc -O3 -DMOLLENOS -D$(arch)

# Kernel + Kernel environment compilation flags
export ASFLAGS = -f bin
export GCFLAGS = $(shared_flags) -ffreestanding $(disable_warnings) $(config_flags)
export GCXXFLAGS = -std=c++17 -ffreestanding $(shared_flags) $(disable_warnings) $(config_flags)

# Shared link flags for everything. /debug:dwarf
export GLFLAGS = /nodefaultlib /machine:X86 /subsystem:native

# Userspace environment compilation flags
export GUCFLAGS = $(shared_flags) $(disable_warnings) $(config_flags)
export GUCXXFLAGS = -std=c++17 $(shared_flags) $(disable_warnings) $(config_flags)
export GUCLIBRARIES = ../lib/libcrt.lib ../lib/libclang.lib ../lib/libc.lib ../lib/libunwind.lib
export GUCXXLIBRARIES = ../lib/libcxx.lib ../lib/libclang.lib ../lib/libc.lib ../lib/libunwind.lib

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

# Build the deploy directory, which contains the primary (system) drive
# structure, system folder, default binaries etc
.PHONY: install_shared
install_shared:
	mkdir -p deploy/hdd
	mkdir -p deploy/hdd/shared
	mkdir -p deploy/hdd/system
	cp -a resources/system/. deploy/hdd/system/
	cp -a resources/shared/. deploy/hdd/shared/
	cp -a boot/build/. deploy/
	./rd $(arch) initrd.mos
	./lzss c initrd.mos deploy/hdd/system/initrd.mos
	./lzss c kernel/build/syskrnl.mos deploy/hdd/system/syskrnl.mos
	cp librt/build/*.lib deploy/hdd/shared/lib/
	cp librt/build/*.dll deploy/hdd/shared/bin/
	cp librt/deploy/*.lib deploy/hdd/shared/lib/
	cp librt/deploy/*.dll deploy/hdd/shared/bin/
	cp userspace/build/*.app deploy/hdd/shared/bin/ 2>/dev/null || :
	cp userspace/build/*.dll deploy/hdd/shared/bin/ 2>/dev/null || :

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
