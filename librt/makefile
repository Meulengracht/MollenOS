# Makefile for building all run-time and support libraries
# Libraries include
# - libk (Kernel C-Library)
# - libc (Userspace C-Library)
# - libm (Userspace Math Library)
# - libos (Userspace OS Support Library)
# 
# The remaining libraries are run-time libraries for C++ support

.PHONY: all
all: build/libk.lib build/libm.dll build/libc.dll build/libos.dll

build:
	mkdir -p $@

build/libk.lib:
	$(MAKE) -C libc -f makefile libk

build/libm.dll:
	$(MAKE) -C libm -f makefile

build/libc.dll:
	$(MAKE) -C libc -f makefile libc

build/libos.dll:
	$(MAKE) -C libos -f makefile clean

.PHONY: clean
clean:
	$(MAKE) -C libm -f makefile clean
	$(MAKE) -C libc -f makefile clean
	$(MAKE) -C libos -f makefile clean
	rm -rf build