# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers

# Include all the definitions for os
include ../../../../config/common.mk

SOURCES = $(wildcard ../common/*.c) \
		  $(wildcard structures/*.c) \
		  $(wildcard *.c)

INCLUDES = -I../../../../librt/include
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /lldmap /entry:__CrtModuleEntry /dll ../../../../librt/build/libc.lib ../../../../librt/build/libdrv.lib

.PHONY: all
all: ../../../build/uhci.dll ../../../build/uhci.mdrv

../../../build/uhci.dll: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../../../build/uhci.mdrv: uhci.mdrv
	@printf "%b" "\033[1;35mCopying settings file " $< "\033[m\n"
	@cp $< $@

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../../../build/uhci.dll
	@rm -f ../../../build/uhci.lib
	@rm -f ../../../build/uhci.mdrv
	@rm -f $(OBJECTS)