# Makefile for building the mollenos usbmanager service
# The usbmanager service takes care of all active usbdevices and controllers
INCLUDES = -I../../librt/include -Iinclude
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__SERVICE_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /lldmap /entry:__CrtServiceEntry /dll ../../librt/build/libsrv.lib ../../librt/build/libc.lib ../../librt/build/libusb.lib

.PHONY: all
all: ../build/usbmanager.dll ../build/usbmanager.mdrv

../build/usbmanager.mdrv: usbmanager.mdrv
	cp $< $@

../build/usbmanager.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJECTS)
	rm -f ../build/usbmanager.dll
	rm -f ../build/usbmanager.mdrv