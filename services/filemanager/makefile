# Makefile for building the mollenos filemanager service
# The filemanager service takes care of all opened files and file-requests

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../../librt/include -Iinclude
SOURCES = $(wildcard layouts/*.c) \
		  $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__SERVICE_IMPL -D__FILEMANAGER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:__CrtServiceEntry /dll /lldmap ../../librt/build/libsrv.lib ../../librt/build/libc.lib

.PHONY: all
all: ../build/filemanager.dll ../build/filemanager.mdrv

../build/filemanager.mdrv: filemanager.mdrv
	@printf "%b" "\033[1;35mCopying settings file " $< "\033[m\n"
	@cp $< $@

../build/filemanager.dll: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f $(OBJECTS)
	@rm -f ../build/filemanager.dll
	@rm -f ../build/filemanager.lib
	@rm -f ../build/filemanager.mdrv