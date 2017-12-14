# Makefile for building the mollenos windowmanager service
# The windowmanager service takes care of all opened files and file-requests
INCLUDES = -I../../librt/include
SOURCES = $(wildcard core/*.c) \
		  $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__WINDOWMANAGER_EXPORT -D__SERVICE_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:_mDrvCrt /dll ../../librt/build/libsrv.lib ../../librt/build/libc.lib

.PHONY: all
all: ../build/windowmanager.dll

../build/windowmanager.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJECTS)
	rm -f ../build/windowmanager.dll
	rm -f ../build/windowmanager.lib