# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers
SOURCES = $(wildcard ../common/*.c) \
		  $(wildcard ./*.c)

INCLUDES = -I../../../../librt/include
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D_DLL -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /lldmap /entry:__CrtModuleEntry /dll ../../../../librt/build/libc.lib ../../../../librt/build/libdrv.lib

.PHONY: all
all: ../../../build/uhci.dll ../../../build/uhci.mdrv

../../../build/uhci.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../../../build/uhci.mdrv: uhci.mdrv
	cp $< $@

%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f ../../../build/uhci.dll
	rm -f ../../../build/uhci.lib
	rm -f ../../../build/uhci.mdrv
	rm -f $(OBJECTS)