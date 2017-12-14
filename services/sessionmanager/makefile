# Makefile for building the mollenos sessionmanager service
# The sessionmanager service takes care of all active users
INCLUDES = -I../../librt/include -Iinclude
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__SERVICE_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /lldmap /entry:_mDrvCrt /dll ../../librt/build/libsrv.lib ../../librt/build/libc.lib

.PHONY: all
all: ../build/sessionmanager.dll ../build/sessionmanager.mdrv

../build/sessionmanager.mdrv: sessionmanager.mdrv
	cp $< $@

../build/sessionmanager.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJECTS)
	rm -f ../build/sessionmanager.dll
	rm -f ../build/sessionmanager.mdrv