# Makefile for building the mollenos sessionmanager service
# The sessionmanager service takes care of all active users

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -Iinclude \
		   -I../../librt/libgracht/include \
		   -I../../librt/libusb/include \
		   -I../../librt/libds/include \
		   -I../../librt/libddk/include \
		   -I../../librt/libc/include \
		   -I../../librt/include

SOURCES = svc_session_protocol_server.c $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

LIBRARIES = ../../librt/deploy/libgracht.lib \
			../../librt/build/libsrv.lib \
			../../librt/build/ddk.lib \
			../../librt/build/libds.lib \
			../../librt/build/c.lib \
			../../librt/build/compiler-rt.lib

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__SERVICE_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /lldmap /entry:__CrtServiceEntry /dll

.PHONY: all
all: ../build/sessionmanager.dll ../build/sessionmanager.mdrv

svc_session_protocol_server.c: ../../protocols/service_protocols.xml
	@printf "%b" "\033[0;36mRegenerating protocol " $@ "\033[m\n"
	python $(generator_path) --protocol $< --lang-c --server --include session

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
	@rm -f svc_*
	@rm -f ../build/sessionmanager.dll
	@rm -f ../build/sessionmanager.lib
	@rm -f ../build/sessionmanager.mdrv