# Makefile for building the mollenos processmanager service
# The processmanager service takes care of all active processes

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../../librt/include -I../../librt/libc/include -I../../librt/libds/include -I../../librt/libddk/include -Iinclude
SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__SERVICE_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /lldmap /entry:__CrtServiceEntry /dll ../../librt/build/libsrv.lib ../../librt/build/libc.lib ../../librt/build/libds.lib

.PHONY: all
all: ../build/processmanager.dll ../build/processmanager.mdrv

../build/processmanager.mdrv: processmanager.mdrv
	@printf "%b" "\033[1;35mCopying settings file " $< "\033[m\n"
	@cp $< $@

../build/processmanager.dll: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f $(OBJECTS)
	@rm -f ../build/processmanager.dll
	@rm -f ../build/processmanager.lib
	@rm -f ../build/processmanager.mdrv