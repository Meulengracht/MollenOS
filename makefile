# Primary make-file for building and preparing the OS image
# Required tools for building
# - nasm assembler
# - clang cross-compiler (requires cmake, svn, libelf)
# - monodevelop
# Required environmental stuff:
# - CROSS=/path/to/cross/home
#

# MollenOS Configuration, comment in or out for specific features
config_flags = 

# Don't load drivers, run it without for debug
#config_flags += -D__OSCONFIG_NODRIVERS


export arch = i386
export CC = $(CROSS)/bin/clang
export CXX = $(CROSS)/bin/clang++
export LD = $(CROSS)/bin/lld-link
export LIB = $(CROSS)/bin/llvm-lib
export ASFLAGS = -f bin
export AS = nasm
export GCFLAGS = -Wall -Wno-self-assign -Wno-unused-function -fms-extensions -ffreestanding -nostdlib -O3 -DMOLLENOS -D$(arch) $(config_flags)
export GCXXFLAGS = -Wall -Wno-self-assign -Wno-unused-function -ffreestanding -nostdlib -O3 -DMOLLENOS -D$(arch) $(config_flags)
export FCOPY = cp
target = img

.PHONY: all
all: boot_loader libraries kernel drivers tools initrd

.PHONY: initrd
initrd:
	mkdir -p initrd
	$(FCOPY) librt/build/*.dll initrd/
	$(FCOPY) services/build/*.dll initrd/
	$(FCOPY) services/build/*.mdrv initrd/
	$(FCOPY) modules/build/*.dll initrd/
	$(FCOPY) modules/build/*.mdrv initrd/

.PHONY: tools
tools:
	$(MAKE) -C tools/lzss -f makefile
	$(MAKE) -C tools/rd -f makefile
	$(MAKE) -C tools/diskutility -f makefile

.PHONY: kernel
kernel:
	$(MAKE) -C kernel -f makefile

.PHONY: drivers
drivers:
	$(MAKE) -C services -f makefile
	$(MAKE) -C modules -f makefile

.PHONY: libraries
libraries:
	$(MAKE) -C librt -f makefile

.PHONY: boot_loader
boot_loader:
	$(MAKE) -C boot -f makefile

.PHONY: install
install:
	mkdir -p deploy/hdd
	mkdir -p deploy/hdd/shared
	mkdir -p deploy/hdd/system
	$(FCOPY) -a resources/system/. deploy/hdd/system/
	$(FCOPY) -a resources/shared/. deploy/hdd/shared/
	$(FCOPY) -a boot/build/. deploy/
	$(FCOPY) librt/build/*.dll deploy/hdd/system/
	./rd $(arch) initrd.mos
	./lzss c initrd.mos deploy/hdd/system/initrd.mos
	./lzss c kernel/build/syskrnl.mos deploy/hdd/system/syskrnl.mos
	mono diskutility -auto -target $(target) -scheme mbr

.PHONY: clean
clean:
	$(MAKE) -C boot -f makefile clean
	$(MAKE) -C librt -f makefile clean
	$(MAKE) -C services -f makefile clean
	$(MAKE) -C modules -f makefile clean
	$(MAKE) -C kernel -f makefile clean
	$(MAKE) -C tools/lzss -f makefile clean
	$(MAKE) -C tools/rd -f makefile clean
	$(MAKE) -C tools/diskutility -f makefile clean
	rm -f initrd.mos
	rm -rf deploy
	rm -rf initrd
	rm -f *.vmdk
	rm -f *.img
