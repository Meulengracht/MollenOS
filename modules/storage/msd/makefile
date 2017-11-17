# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers
SOURCES = $(wildcard ./protocols/*.c) \
		  $(wildcard ./*.c)

INCLUDES = -I../../../librt/include -I../sata
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -D_DLL -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:_mDrvCrt /dll ../../../librt/build/libc.lib ../../../librt/build/libdrv.lib ../../../librt/build/libos.lib ../../../librt/build/libusb.lib

.PHONY: all
all: ../../build/msd.dll ../../build/msd.mdrv

../../build/msd.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../../build/msd.mdrv: msd.mdrv
	cp $< $@

%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f ../../build/msd.dll
	rm -f ../../build/msd.lib
	rm -f ../../build/msd.mdrv
	rm -f $(OBJECTS)