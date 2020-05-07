# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers

# Include all the definitions for os
include ../../../../config/common.mk

INCLUDES = -I../../../../librt/include \
		   -I../../../../librt/libc/include \
		   -I../../../../librt/libds/include \
		   -I../../../../librt/libddk/include \
		   -I../../../../librt/libusb/include \
		   -I../../../../librt/libgracht/include

LIBRARIES = ../../../../librt/build/ddk.lib \
			../../../../librt/deploy/libgracht.lib \
			../../../../librt/build/c.lib \
			../../../../librt/build/libdrv.lib \
			../../../../librt/build/compiler-rt.lib

SOURCES = $(wildcard ../common/*.c) \
		  $(wildcard structures/*.c) \
		  $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:__CrtModuleEntry /dll

.PHONY: all
all: ../../../build/ehci.dll ../../../build/ehci.mdrv

../../../build/ehci.dll: $(OBJECTS) $(LIBRARIES)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) $(LIBRARIES) /out:$@

../../../build/ehci.mdrv: ehci.mdrv
	@printf "%b" "\033[1;35mCopying settings file " $< "\033[m\n"
	@cp $< $@

%.o : %.c
	@printf "%b" "\033[0;32mCompiling source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../../../build/ehci.dll
	@rm -f ../../../build/ehci.lib
	@rm -f ../../../build/ehci.mdrv
	@rm -f $(OBJECTS)