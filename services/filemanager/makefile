# Makefile for building the mollenos filemanager service
# The filemanager service takes care of all opened files and file-requests
INCLUDES = -I../../librt/include -Iinclude
SOURCES = $(wildcard layouts/*.c) \
		  $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__SERVICE_IMPL -D__FILEMANAGER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:__CrtServiceEntry /dll /lldmap ../../librt/build/libsrv.lib ../../librt/build/libc.lib

.PHONY: all
all: ../build/filemanager.dll ../build/filemanager.mdrv

../build/filemanager.mdrv: filemanager.mdrv
	cp $< $@

../build/filemanager.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJECTS)
	rm -f ../build/filemanager.dll
	rm -f ../build/filemanager.mdrv