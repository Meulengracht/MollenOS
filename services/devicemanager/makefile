# Makefile for building the mollenos devicemanager service
# The devicemanager service takes care of devices present in system
INCLUDES = -I../../librt/include -Iarch/$(arch)
SOURCES = $(wildcard arch/$(arch)/*.c) \
		  main.c
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__SERVICE_IMPL -D__DEVICEMANAGER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:_mDrvCrt /dll ../../librt/build/libsrv.lib ../../librt/build/libc.lib ../../librt/build/libos.lib

.PHONY: all
all: ../build/devicemanager.dll ../build/devicemanager.mdrv

../build/devicemanager.mdrv: devicemanager.mdrv
	cp $< $@

../build/devicemanager.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJECTS)
	rm -f ../build/devicemanager.dll
	rm -f ../build/devicemanager.lib
	rm -f ../build/devicemanager.mdrv