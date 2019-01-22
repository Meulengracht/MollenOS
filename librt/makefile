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
all: build deploy
	@$(MAKE) -s -C libc -f makefile libk
	@$(MAKE) -s -C libds -f makefile
	@$(MAKE) -s -C libclang -f makefile
	@$(MAKE) -s -C libcrt -f makefile libcrt
	@$(MAKE) -s -C libcrt -f makefile libdrv
	@$(MAKE) -s -C libcrt -f makefile libsrv
	@$(MAKE) -s -C libm -f makefile
	@$(MAKE) -s -C libc -f makefile libc
	@$(MAKE) -s -C libacpi -f makefile
	@$(MAKE) -s -C libusb -f makefile
	@$(MAKE) -s -C libcxx -f makefile

# Static code analysis target for clang-tidy, we only care about the 
# libraries we've coded our self, skip libclang, libacpi, libcxx and libm.
# We also ignore libcrt
.PHONY: tidy
tidy: 
	@$(MAKE) -s -C libc -f makefile tidy
	@$(MAKE) -s -C libds -f makefile tidy
	@$(MAKE) -s -C libusb -f makefile tidy

build:
	@mkdir -p $@

deploy:
	@mkdir -p $@

.PHONY: clean
clean:
	@$(MAKE) -s -C libm -f makefile clean
	@$(MAKE) -s -C libds -f makefile clean
	@$(MAKE) -s -C libc -f makefile clean
	@$(MAKE) -s -C libacpi -f makefile clean
	@$(MAKE) -s -C libusb -f makefile clean
	@$(MAKE) -s -C libclang -f makefile clean
	@$(MAKE) -s -C libcrt -f makefile clean
	@$(MAKE) -s -C libcxx -f makefile clean
	@rm -rf build
	@rm -rf deploy