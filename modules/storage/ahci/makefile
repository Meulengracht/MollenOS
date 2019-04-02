# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers

# Include all the definitions for os
include ../../../config/common.mk

SOURCES = $(wildcard ./*.c)

INCLUDES = -I../../../librt/include -I../../../librt/libc/include -I../../../librt/libds/include -I../../../librt/libddk/include -I../sata
OBJECTS = $(SOURCES:.c=.o)

LIBRARIES = ../../../librt/build/ddk.lib ../../../librt/build/c.lib ../../../librt/build/libdrv.lib
CFLAGS = $(GCFLAGS) -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /lldmap /subsystem:native /entry:__CrtModuleEntry /dll

.PHONY: all
all: ../../build/ahci.dll ../../build/ahci.mdrv

../../build/ahci.dll: $(OBJECTS) $(LIBRARIES)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) $(LIBRARIES) /out:$@

../../build/ahci.mdrv: ahci.mdrv
	@printf "%b" "\033[1;35mCopying settings file " $< "\033[m\n"
	@cp $< $@

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../../build/ahci.dll
	@rm -f ../../build/ahci.lib
	@rm -f ../../build/ahci.mdrv
	@rm -f $(OBJECTS)