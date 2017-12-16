# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers
SOURCES = $(wildcard ./*.c)

INCLUDES = -I../../../librt/include -I../sata
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -D_DLL -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:__CrtModuleEntry /dll ../../../librt/build/libc.lib ../../../librt/build/libdrv.lib

.PHONY: all
all: ../../build/ahci.dll ../../build/ahci.mdrv

../../build/ahci.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../../build/ahci.mdrv: ahci.mdrv
	cp $< $@

%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f ../../build/ahci.dll
	rm -f ../../build/ahci.lib
	rm -f ../../build/ahci.mdrv
	rm -f $(OBJECTS)