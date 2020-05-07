# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers

# Include all the definitions for os
include ../../../config/common.mk

SOURCES = ctt_driver_protocol_server.c ctt_storage_protocol_server.c \
		  $(wildcard ./protocols/*.c) \
		  $(wildcard ./*.c)
OBJECTS = $(SOURCES:.c=.o)

INCLUDES = -I../../../librt/include \
		   -I../../../librt/libc/include \
		   -I../../../librt/libds/include \
		   -I../../../librt/libddk/include \
		   -I../../../librt/libusb/include \
		   -I../../../librt/libgracht/include \
		   -I../sata

LIBRARIES = ../../../librt/build/ddk.lib \
			../../../librt/build/libusb.lib \
			../../../librt/build/libds.lib \
			../../../librt/deploy/libgracht.lib \
			../../../librt/build/c.lib \
			../../../librt/build/compiler-rt.lib \
			../../../librt/build/libdrv.lib
CFLAGS = $(GCFLAGS) -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /lldmap /subsystem:native /entry:__CrtModuleEntry /dll

.PHONY: all
all: ../../build/msd.dll ../../build/msd.mdrv

ctt_driver_protocol_server.c: ../../../protocols/contract_protocols.xml
	@printf "%b" "\033[0;36mRegenerating protocol " $@ "\033[m\n"
	python ../../../protocols/gracht_generator.py --protocol $< --lang-c --server --include driver

ctt_storage_protocol_server.c: ../../../protocols/contract_protocols.xml
	@printf "%b" "\033[0;36mRegenerasting protocol " $@ "\033[m\n"
	python ../../../protocols/gracht_generator.py --protocol $< --lang-c --server --include storage

../../build/msd.dll: $(OBJECTS) $(LIBRARIES)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) $(LIBRARIES) /out:$@

../../build/msd.mdrv: msd.mdrv
	@printf "%b" "\033[1;35mCopying settings file " $< "\033[m\n"
	@cp $< $@

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../../build/msd.dll
	@rm -f ../../build/msd.lib
	@rm -f ../../build/msd.mdrv
	@rm -f ctt_*
	@rm -f $(OBJECTS)