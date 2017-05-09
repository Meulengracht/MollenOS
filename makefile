# Definitions
export CC = clang
export CXX = clang++
export LD = lld
export ASFLAGS = -f bin
export AS = nasm
export DEBUG = -g
export FCOPY = cp
export arch = i386
target = vmdk

all: boot_loader libraries kernel initrd
	$(FCOPY) boot/build/stage1.sys install/stage1.sys
	$(FCOPY) boot/build/stage2.sys install/stage2.sys
	$(FCOPY) librt/build/*.dll modules/build/
	$(FCOPY) librt/build/*.dll install/hdd/system/
	$(FCOPY) kernel/build/syskrnl.mos install/hdd/system/syskrnl.mos
	$(FCOPY) modules/initrd.mos install/hdd/system/initrd.mos

.PHONY: initrd
initrd:
	modules/rdbuilder.exe

.PHONY: kernel
kernel:
	$(MAKE) -C kernel -f makefile

.PHONY: libraries
libraries:
	$(MAKE) -C librt -f makefile

.PHONY: boot_loader
boot_loader:
	$(MAKE) -C boot -f makefile

.PHONY: install
install:
	install/diskutility.exe -auto -target $(target) -scheme mbr

.PHONY: clean
clean:
	$(MAKE) -C boot -f makefile clean
	$(MAKE) -C librt -f makefile clean
	$(MAKE) -C kernel-f makefile clean
	rm *.vmdk
	rm *.img
