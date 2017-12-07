# Makefile for building all run-time and support libraries
# Libraries include
# - libk (Kernel C-Library)
# - libds\libdsk (Datastructure C-Library)
# - libcrt (Userspace CRT-Library)
# - libc (Userspace C-Library)
# - libm (Userspace Math Library)
# - libos (Userspace OS Support Library)
# 
# The remaining libraries are run-time libraries for C++ support

.PHONY: all
all: build deploy build/libk.lib build/libds.lib build/libdsk.lib build/libcrt.lib build/libdrv.lib build/libsrv.lib build/libm.dll build/libc.dll build/libos.dll build/libusb.lib deploy/libunwind.dll deploy/libcxxabi.dll

build:
	mkdir -p $@

deploy:
	mkdir -p $@

build/libk.lib:
	$(MAKE) -C libc -f makefile libk

build/libds.lib:
	$(MAKE) -C libds -f makefile libds

build/libds.lib:
	$(MAKE) -C libds -f makefile libdsk

build/libcrt.lib:
	$(MAKE) -C libcrt -f makefile libcrt

build/libdrv.lib:
	$(MAKE) -C libcrt -f makefile libdrv

build/libsrv.lib:
	$(MAKE) -C libcrt -f makefile libsrv

build/libm.dll:
	$(MAKE) -C libm -f makefile

build/libc.dll:
	$(MAKE) -C libc -f makefile libc

build/libos.dll:
	$(MAKE) -C libos -f makefile

build/libusb.lib:
	$(MAKE) -C libusb -f makefile

deploy/libunwind.dll:
	$(MAKE) -C libunwind -f makefile

deploy/libcxxabi.dll:
	$(MAKE) -C libcxxabi -f makefile

.PHONY: clean
clean:
	$(MAKE) -C libm -f makefile clean
	$(MAKE) -C libds -f makefile clean
	$(MAKE) -C libc -f makefile clean
	$(MAKE) -C libos -f makefile clean
	$(MAKE) -C libusb -f makefile clean
	$(MAKE) -C libcrt -f makefile clean
	$(MAKE) -C libunwind -f makefile clean
	$(MAKE) -C libcxxabi -f makefile clean
	@rm -rf build
	@rm -rf deploy