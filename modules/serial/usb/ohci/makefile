# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers

# Include all the definitions for os
include ../../../../config/common.mk

SOURCES = $(wildcard ../common/*.c) \
		  $(wildcard structures/*.c) \
		  $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

INCLUDES = -I../../../../librt/include \
		   -I../../../../librt/libc/include \
		   -I../../../../librt/libds/include \
		   -I../../../../librt/libddk/include \
		   -I../../../../librt/libgracht/include

LIBRARIES = ../../../../librt/build/ddk.lib \
			../../../../librt/deploy/libgracht.lib \
			../../../../librt/build/c.lib \
			../../../../librt/build/libdrv.lib \
			../../../../librt/build/compiler-rt.lib

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:__CrtModuleEntry /dll /lldmap

.PHONY: all
all: ../../../build/ohci.dll ../../../build/ohci.mdrv

../../../build/ohci.dll: $(OBJECTS) $(LIBRARIES)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) $(LIBRARIES) /out:$@

../../../build/ohci.mdrv: ohci.mdrv
	@printf "%b" "\033[1;35mCopying settings file " $< "\033[m\n"
	@cp $< $@

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../../../build/ohci.dll
	@rm -f ../../../build/ohci.lib
	@rm -f ../../../build/ohci.mdrv
	@rm -f $(OBJECTS)