# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers

# Include all the definitions for os
include ../../../config/common.mk

SOURCES = $(wildcard ./*.c)

INCLUDES = -I../../../librt/include -I../../../librt/libc/include -I../../../librt/libds/include -I../../../librt/libddk/include
OBJECTS = $(SOURCES:.c=.o)

LIBRARIES = ../../../librt/build/ddk.lib ../../../librt/build/libc.lib ../../../librt/build/libdrv.lib ../../../librt/build/libusb.lib
CFLAGS = $(GCFLAGS) -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:__CrtModuleEntry /dll

.PHONY: all
all: ../../build/hid.dll ../../build/hid.mdrv

../../build/hid.dll: $(OBJECTS) $(LIBRARIES)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) $(LIBRARIES) /out:$@

../../build/hid.mdrv: hid.mdrv
	@printf "%b" "\033[1;35mCopying settings file " $< "\033[m\n"
	@cp $< $@

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../../build/hid.dll
	@rm -f ../../build/hid.lib
	@rm -f ../../build/hid.mdrv
	@rm -f $(OBJECTS)