# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers
SOURCES = $(wildcard ./*.c)

INCLUDES = -I../../../librt/include -I../sata
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:__CrtModuleEntry /dll ../../../librt/build/libc.lib ../../../librt/build/libdrv.lib ../../../librt/build/libusb.lib

.PHONY: all
all: ../../build/hid.dll ../../build/hid.mdrv

../../build/hid.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../../build/hid.mdrv: hid.mdrv
	cp $< $@

%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f ../../build/hid.dll
	rm -f ../../build/hid.lib
	rm -f ../../build/hid.mdrv
	rm -f $(OBJECTS)