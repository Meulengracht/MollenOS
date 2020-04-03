# Makefile for building the standard c-libraries for both userspace and kernel
# - libddk

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../../librt/libgracht/include -I../include -I../libc/include -I../libds/include -Iinclude

# Setup architecture specific flags and paths
ifeq ($(VALI_ARCH), i386)
	ASFLAGS = -f win32 -Xvc
else ifeq ($(VALI_ARCH), amd64)
	ASFLAGS = -f win64 -Xvc
else
$(error VALI_ARCH is not set to a valid value)
endif

PROTOCOLS_C = protocols/sys_session_protocol_client.c \
			  protocols/sys_storage_protocol_client.c \
			  protocols/sys_file_protocol_client.c \
			  protocols/sys_path_protocol_client.c \
			  protocols/sys_process_protocol_client.c \
			  protocols/sys_library_protocol_client.c \
			  protocols/sys_socket_protocol_client.c

ASM_SOURCES = $(wildcard arch/$(VALI_ARCH)/*.s)
SOURCES = $(wildcard **/*.c) $(wildcard *.c)
OBJECTS = $(PROTOCOLS_C:.c=.o) $(SOURCES:.c=.o) $(ASM_SOURCES:.s=.o)

# Setup flags
CFLAGS = $(GCFLAGS) $(INCLUDES)
LFLAGS = /lib

# default-target
.PHONY: all
all: protocols ../build/ddk.lib

protocols:
	mkdir -p $@

protocols/sys_session_protocol_client.c: ../../protocols/service_protocols.xml
	@printf "%b" "\033[0;36mRegenerating protocol " $@ "\033[m\n"
	python ../../protocols/gracht_generator.py --protocol $< --out $(dir $@) --lang-c --client

../build/ddk.lib: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.c
	@mkdir -p $(dir $@)
	@printf "%b" "\033[0;32m[LIBDDK] Compiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

%.o : %.s
	@mkdir -p $(dir $@)
	@printf "%b" "\033[0;32m[LIBDDK] Assembling source object " $< "\033[m\n"
	@$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	@rm -rf protocols
	@rm -f ../build/ddk.lib
	@rm -f $(OBJECTS)