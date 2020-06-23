# Makefile for building the mollenos filemanager service
# The filemanager service takes care of all opened files and file-requests

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -Iinclude \
		   -I../../librt/libgracht/include \
		   -I../../librt/libusb/include \
		   -I../../librt/libds/include \
		   -I../../librt/libddk/include \
		   -I../../librt/libddk/protocols \
		   -I../../librt/include -I../../librt/libc/include

PROTOCOLS = svc_file_protocol_server.c \
			svc_storage_protocol_server.c \
			svc_path_protocol_server.c

SOURCES = $(wildcard layouts/*.c) \
		  $(wildcard *.c)

OBJECTS := $(PROTOCOLS:.c=.o) $(SOURCES:.c=.o)

LIBRARIES = ../../librt/deploy/libgracht.lib \
			../../librt/build/libsrv.lib \
			../../librt/build/ddk.lib \
			../../librt/build/libds.lib \
			../../librt/build/c.lib \
			../../librt/build/compiler-rt.lib

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__SERVICE_IMPL -D__FILEMANAGER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:__CrtServiceEntry /dll /lldmap

.PHONY: all
all: ../build/filemanager.dll ../build/filemanager.mdrv

svc_file_protocol_server.c: ../../protocols/service_protocols.xml
	@printf "%b" "\033[0;36mRegenerating protocol " $@ "\033[m\n"
	python $(generator_path) --protocol $< --lang-c --server --include file

svc_storage_protocol_server.c: ../../protocols/service_protocols.xml
	@printf "%b" "\033[0;36mRegenerating protocol " $@ "\033[m\n"
	python $(generator_path) --protocol $< --lang-c --server --include storage
	
svc_path_protocol_server.c: ../../protocols/service_protocols.xml
	@printf "%b" "\033[0;36mRegenerating protocol " $@ "\033[m\n"
	python $(generator_path) --protocol $< --lang-c --server --include path

../build/filemanager.mdrv: filemanager.mdrv
	@printf "%b" "\033[1;35mCopying settings file " $< "\033[m\n"
	@cp $< $@

../build/filemanager.dll: $(OBJECTS) $(LIBRARIES)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(LIBRARIES) $(OBJECTS) /out:$@
	
%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f $(OBJECTS)
	@rm -f svc_*
	@rm -f ../build/filemanager.dll
	@rm -f ../build/filemanager.lib
	@rm -f ../build/filemanager.mdrv