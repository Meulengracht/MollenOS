# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers

# Include all the definitions for os
include ../../../config/common.mk

INCLUDES = -I../../../librt/include \
		   -I../../../librt/libc/include \
		   -I../../../librt/libds/include \
		   -I../../../librt/libddk/include \
		   -I../../../librt/libgracht/include

LIBRARIES = ../../../librt/deploy/libgracht.lib \
			../../../librt/build/compiler-rt.lib \
			../../../librt/build/ddk.lib \
			../../../librt/build/c.lib \
			../../../librt/build/libdrv.lib \
			../../../librt/build/libds.lib

SOURCES = $(wildcard ./*.c)
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /noentry /dll /lldmap 

.PHONY: all
all: ../../build/mfs.dll ../../build/mfs.mdrv

../../build/mfs.dll: $(OBJECTS) $(LIBRARIES)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) $(LIBRARIES) /out:$@

../../build/mfs.mdrv: mfs.mdrv
	@printf "%b" "\033[1;35mCopying settings file " $< "\033[m\n"
	@cp $< $@

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../../build/mfs.dll
	@rm -f ../../build/mfs.lib
	@rm -f ../../build/mfs.mdrv
	@rm -f $(OBJECTS)