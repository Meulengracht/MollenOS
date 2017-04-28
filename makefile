# Definitions
export CC = clang
export CXX = clang++
export LD = lld
export ASM = nasm -f bin
export DEBUG = -g
export FCOPY = copy
export arch = i386
target = vmdk

CFLAGS = -Wall -c $(DEBUG)
LFLAGS = /nodefaultlib /subsystem:native /entry:_kmain

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
	$(MAKE) -C boot/make clean
	$(MAKE) -C librt/make clean
	$(MAKE) -C kernel/make clean
	rm *.vmdk
	rm *.img
