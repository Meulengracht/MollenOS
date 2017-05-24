# Definitions - /entry:_kmain 
export arch = i386
export CC = $(CROSS)/bin/clang
export CXX = $(CROSS)/bin/clang++
export LD = $(CROSS)/bin/lld-link
export LIB = $(CROSS)/bin/llvm-lib
export ASFLAGS = -f bin
export AS = nasm
export GCFLAGS = -Wall -Wno-self-assign -Wno-unused-function -fms-extensions -ffreestanding -g -nostdlib -O2 -DMOLLENOS -D$(arch)
export GCXXFLAGS = -Wall -Wno-self-assign -Wno-unused-function -ffreestanding -g -nostdlib -O2 -DMOLLENOS -D$(arch)
export FCOPY = cp
target = vmdk

.PHONY: all
all: boot_loader libraries kernel drivers initrd

.PHONY: initrd
initrd:
	mkdir -p initrd
	$(FCOPY) librt/build/*.dll initrd/
	$(FCOPY) services/build/*.dll initrd/
	$(FCOPY) services/build/*.mdrv initrd/
	$(FCOPY) modules/build/*.dll initrd/
	$(FCOPY) modules/build/*.mdrv initrd/

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
	$(FCOPY) -a resources/. deploy/hdd/system/
	$(FCOPY) -a boot/build/. deploy/
	$(FCOPY) librt/build/*.dll deploy/hdd/system/
	$(FCOPY) kernel/build/syskrnl.mos deploy/hdd/system/syskrnl.mos

.PHONY: clean
clean:
	$(MAKE) -C boot -f makefile clean
	$(MAKE) -C librt -f makefile clean
	$(MAKE) -C services -f makefile clean
	$(MAKE) -C modules -f makefile clean
	$(MAKE) -C kernel -f makefile clean
	rm -rf deploy
	rm -rf initrd
	rm -f *.vmdk
	rm -f *.img
