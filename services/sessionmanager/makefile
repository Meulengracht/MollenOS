# Makefile for building the mollenos sessionmanager service
# The sessionmanager service takes care of all active users

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../../librt/include -I../../librt/libc/include -I../../librt/libds/include -I../../librt/libddk/include -Iinclude
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)
LIBRARIES = ../../librt/build/libsrv.lib ../../librt/build/ddk.lib ../../librt/build/c.lib

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__SERVICE_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /lldmap /entry:__CrtServiceEntry /dll

.PHONY: all
all: ../build/sessionmanager.dll ../build/sessionmanager.mdrv

../build/sessionmanager.mdrv: sessionmanager.mdrv
	@printf "%b" "\033[1;35mCopying settings file " $< "\033[m\n"
	@cp $< $@

../build/sessionmanager.dll: $(OBJECTS) $(LIBRARIES)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(LIBRARIES) $(OBJECTS) /out:$@
	
%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f $(OBJECTS)
	@rm -f ../build/sessionmanager.dll
	@rm -f ../build/sessionmanager.lib
	@rm -f ../build/sessionmanager.mdrv