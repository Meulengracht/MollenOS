# Makefile for building the mollenos devicemanager service
# The devicemanager service takes care of devices present in system
INCLUDES = -I../../librt/include -Iarch/$(VALI_ARCH)
SOURCES = $(wildcard arch/$(VALI_ARCH)/*.c) \
		  main.c
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__SERVICE_IMPL -D__DEVICEMANAGER_IMPL $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap /entry:__CrtServiceEntry /dll ../../librt/build/libsrv.lib ../../librt/build/libc.lib

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