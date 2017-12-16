# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers
SOURCES = $(wildcard ../common/*.c) \
		  $(wildcard ./*.c)

INCLUDES = -I../../../../librt/include
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D_DLL -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:__CrtModuleEntry /dll ../../../../librt/build/libc.lib ../../../../librt/build/libdrv.lib

.PHONY: all
all: ../../../build/ehci.dll ../../../build/ehci.mdrv

../../../build/ehci.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../../../build/ehci.mdrv: ehci.mdrv
	cp $< $@

%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f ../../../build/ehci.dll
	rm -f ../../../build/ehci.lib
	rm -f ../../../build/ehci.mdrv
	rm -f $(OBJECTS)