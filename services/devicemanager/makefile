# Makefile for building the mollenos devicemanager service
# The devicemanager service takes care of devices present in system

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../../librt/libgracht/include \
		   -I../../librt/libds/include \
		   -I../../librt/libddk/include \
		   -I../../librt/include -I../../librt/libc/include
SOURCES = svc_device_protocol_server.c main.c

# Check for architecture
ifeq ($(VALI_ARCH), i386)
	INCLUDES += -Iarch/x86
	SOURCES += $(wildcard arch/x86/*.c)
else ifeq ($(VALI_ARCH), amd64)
	INCLUDES += -Iarch/x86
	SOURCES += $(wildcard arch/x86/*.c)
else
$(error VALI_ARCH is not set to a valid value)
endif

OBJECTS = $(SOURCES:.c=.o)
LIBRARIES = ../../librt/deploy/libgracht.lib \
			../../librt/build/libsrv.lib \
			../../librt/build/ddk.lib \
			../../librt/build/c.lib \
			../../librt/build/compiler-rt.lib

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__SERVICE_IMPL $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap /entry:__CrtServiceEntry /dll

.PHONY: all
all: ../build/devicemanager.dll ../build/devicemanager.mdrv

svc_device_protocol_server.c: ../../protocols/service_protocols.xml
	@printf "%b" "\033[0;36mRegenerating protocol " $@ "\033[m\n"
	python ../../protocols/gracht_generator.py --protocol $< --lang-c --server --include device
	
../build/devicemanager.mdrv: devicemanager.mdrv
	@printf "%b" "\033[1;35mCopying settings file " $< "\033[m\n"
	@cp $< $@

../build/devicemanager.dll: $(OBJECTS) $(LIBRARIES)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(LIBRARIES) $(OBJECTS) /out:$@
	
%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f $(OBJECTS)
	@rm -f svc_*
	@rm -f ../build/devicemanager.dll
	@rm -f ../build/devicemanager.lib
	@rm -f ../build/devicemanager.mdrv