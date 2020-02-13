# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers

# Include all the definitions for os
include ../../../../config/common.mk

SOURCES = hid_events_protocol_client.c \
		  $(wildcard devices/scancodes/*.c) \
		  $(wildcard devices/*.c) \
		  $(wildcard ./*.c)

INCLUDES = -I../../../../protocols \
		   -I../../../../librt/include \
		   -I../../../../librt/libc/include \
		   -I../../../../librt/libds/include \
		   -I../../../../librt/libddk/include \
		   -I../../../../librt/libwm/include
OBJECTS = $(SOURCES:.c=.o)

LIBRARIES = ../../../../librt/build/ddk.lib \
			../../../../librt/build/c.lib \
			../../../../librt/build/libdrv.lib \
			../../../../librt/deploy/libwm.lib
CFLAGS = $(GCFLAGS) -Wno-invalid-source-encoding -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:__CrtModuleEntry /lldmap /dll

.PHONY: all
all: ../../../build/ps2.dll ../../../build/ps2.mdrv

hid_events_protocol_client.c: ../../../../protocols/hid_protocol.xml
	@printf "%b" "\033[0;36mRegenerating protocol " $@ "\033[m\n"
	python ../../../../protocols/gracht_generator.py --protocol $< --lang-c --client
	

../../../build/ps2.dll: $(OBJECTS) $(LIBRARIES)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) $(LIBRARIES) /out:$@

../../../build/ps2.mdrv: ps2.mdrv
	@printf "%b" "\033[1;35mCopying settings file " $< "\033[m\n"
	@cp $< $@

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../../../build/ps2.dll
	@rm -f ../../../build/ps2.lib
	@rm -f ../../../build/ps2.mdrv
	@rm -f hid_events_protocol*
	@rm -f $(OBJECTS)