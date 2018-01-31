# Makefile for building all run-time and support libraries
# Libraries include
# - libk (Kernel C-Library)
# - libds\libdsk (Datastructure C-Library)
# - libcrt (Userspace CRT-Library)
# - libc (Userspace C-Library)
# - libm (Userspace Math Library)
# 
# The remaining libraries are run-time libraries for C++ support

.PHONY: all
all: build deploy build/libk.lib build/libds.lib build/libdsk.lib build/libclang.lib build/libcrt.lib build/libdrv.lib build/libsrv.lib build/libm.dll build/libc.dll build/libusb.lib deploy/libunwind.dll deploy/libcxx.dll

build:
	@mkdir -p $@

deploy:
	@mkdir -p $@

build/libk.lib:
	@$(MAKE) -s -C libc -f makefile libk

build/libds.lib:
	@$(MAKE) -s -C libds -f makefile libds

build/libdsk.lib:
	@$(MAKE) -s -C libds -f makefile libdsk

build/libclang.lib:
	@$(MAKE) -s -C libclang -f makefile

build/libcrt.lib:
	@$(MAKE) -s -C libcrt -f makefile libcrt

build/libdrv.lib:
	@$(MAKE) -s -C libcrt -f makefile libdrv

build/libsrv.lib:
	@$(MAKE) -s -C libcrt -f makefile libsrv

build/libm.dll:
	@$(MAKE) -s -C libm -f makefile

build/libc.dll:
	@$(MAKE) -s -C libc -f makefile libc

build/libusb.lib:
	@$(MAKE) -s -C libusb -f makefile

deploy/libunwind.dll:
	@$(MAKE) -s -C libunwind -f makefile

deploy/libcxxabi.dll:
	@$(MAKE) -s -C libcxxabi -f makefile

deploy/libcxx.dll:
	@$(MAKE) -s -C libcxx -f makefile

.PHONY: clean
clean:
	@$(MAKE) -s -C libm -f makefile clean
	@$(MAKE) -s -C libds -f makefile clean
	@$(MAKE) -s -C libc -f makefile clean
	@$(MAKE) -s -C libusb -f makefile clean
	@$(MAKE) -s -C libclang -f makefile clean
	@$(MAKE) -s -C libcrt -f makefile clean
	@$(MAKE) -s -C libunwind -f makefile clean
	@$(MAKE) -s -C libcxx -f makefile clean
	@rm -rf build
	@rm -rf deploy