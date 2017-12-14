# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers
SOURCES = $(wildcard ../common/*.c) \
		  $(wildcard ./*.c)

INCLUDES = -I../../../../librt/include
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D_DLL -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:_mDrvCrt /dll ../../../../librt/build/libc.lib ../../../../librt/build/libdrv.lib

.PHONY: all
all: ../../../build/ohci.dll ../../../build/ohci.mdrv

../../../build/ohci.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../../../build/ohci.mdrv: ohci.mdrv
	cp $< $@

%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f ../../../build/ohci.dll
	rm -f ../../../build/ohci.lib
	rm -f ../../../build/ohci.mdrv
	rm -f $(OBJECTS)