# Makefile for building the mollenos usbmanager service
# The usbmanager service takes care of all active usbdevices and controllers

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../../librt/include -I../../librt/libc/include -I../../librt/libds/include -I../../librt/libddk/include -Iinclude
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)
LIBRARIES = ../../librt/build/libsrv.lib ../../librt/build/ddk.lib ../../librt/build/libc.lib ../../librt/build/libusb.lib

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__SERVICE_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /lldmap /entry:__CrtServiceEntry /dll

.PHONY: all
all: ../build/usbmanager.dll ../build/usbmanager.mdrv

../build/usbmanager.mdrv: usbmanager.mdrv
	@printf "%b" "\033[1;35mCopying settings file " $< "\033[m\n"
	@cp $< $@

../build/usbmanager.dll: $(OBJECTS) $(LIBRARIES)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(LIBRARIES) $(OBJECTS) /out:$@
	
%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f $(OBJECTS)
	@rm -f ../build/usbmanager.dll
	@rm -f ../build/usbmanager.lib
	@rm -f ../build/usbmanager.mdrv