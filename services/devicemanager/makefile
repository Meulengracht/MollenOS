# Makefile for building the mollenos devicemanager service
# The devicemanager service takes care of devices present in system

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../../librt/include -I../../librt/libc/include -I../../librt/libds/include -I../../librt/libddk/include
SOURCES = main.c

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
LIBRARIES = ../../librt/build/libsrv.lib ../../librt/build/ddk.lib ../../librt/build/libc.lib

CFLAGS = $(GCFLAGS) -Wno-address-of-packed-member -D__SERVICE_IMPL $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap /entry:__CrtServiceEntry /dll

.PHONY: all
all: ../build/devicemanager.dll ../build/devicemanager.mdrv

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
	@rm -f ../build/devicemanager.dll
	@rm -f ../build/devicemanager.lib
	@rm -f ../build/devicemanager.mdrv