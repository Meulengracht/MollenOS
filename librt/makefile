# Makefile for building all run-time and support libraries
# Libraries include
# - libk (Kernel C-Library)
# - libcrt (Userspace CRT-Library)
# - libc (Userspace C-Library)
# - libm (Userspace Math Library)
# - libos (Userspace OS Support Library)
# 
# The remaining libraries are run-time libraries for C++ support

.PHONY: all
all: build build/libk.lib build/libcrt.lib build/libdrv.lib build/libsrv.lib build/libm.dll build/libc.dll build/libos.dll build/libcxxabi.dll

build:
	mkdir -p $@

build/libk.lib:
	$(MAKE) -C libc -f makefile libk

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

build/libcxxabi.dll:
	$(MAKE) -C libcxxabi -f makefile

.PHONY: clean
clean:
	$(MAKE) -C libm -f makefile clean
	$(MAKE) -C libc -f makefile clean
	$(MAKE) -C libos -f makefile clean
	$(MAKE) -C libcrt -f makefile clean
	$(MAKE) -C libcxxabi -f makefile clean
	@rm -rf build