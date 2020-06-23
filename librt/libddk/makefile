# Makefile for building the standard c-libraries for both userspace and kernel
# - libddk

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../libgracht/include \
		   -I../libusb/include \
		   -I../libc/include \
		   -I../libds/include \
		   -I../include \
		   -Iprotocols \
		   -Iinclude

# Setup architecture specific flags and paths
ifeq ($(VALI_ARCH), i386)
	ASFLAGS = -f win32 -Xvc
else ifeq ($(VALI_ARCH), amd64)
	ASFLAGS = -f win64 -Xvc
else
$(error VALI_ARCH is not set to a valid value)
endif

PROTOCOLS_C = protocols/svc_session_protocol_client.c \
			  protocols/svc_storage_protocol_client.c \
			  protocols/svc_file_protocol_client.c \
			  protocols/svc_path_protocol_client.c \
			  protocols/svc_process_protocol_client.c \
			  protocols/svc_library_protocol_client.c \
			  protocols/svc_socket_protocol_client.c \
			  protocols/svc_device_protocol_client.c \
			  protocols/svc_usb_protocol_client.c \
			  protocols/ctt_driver_protocol_client.c \
			  protocols/ctt_storage_protocol_client.c \
			  protocols/ctt_usbhost_protocol_client.c

ASM_SOURCES = $(wildcard arch/$(VALI_ARCH)/*.s)
SOURCES = $(wildcard **/*.c) $(wildcard *.c)
OBJECTS = $(PROTOCOLS_C:.c=.o) $(SOURCES:.c=.o) $(ASM_SOURCES:.s=.o)

# Setup flags
CFLAGS = $(GCFLAGS) $(INCLUDES)
LFLAGS = /lib

# default-target
.PHONY: all
all: protocols ../build/ddk.lib

protocols: ../../protocols/service_protocols.xml ../../protocols/contract_protocols.xml
	mkdir -p $@
	@printf "%b" "\033[0;36mRegenerating service protocols \033[m\n"
	python $(generator_path) --protocol ../../protocols/service_protocols.xml --out $@ --lang-c --client
	@printf "%b" "\033[0;36mRegenerating contract protocols \033[m\n"
	python $(generator_path) --protocol ../../protocols/contract_protocols.xml --out $@ --lang-c --client
	@mkdir -p include/ddk/protocols
	cp protocols/*.h include/ddk/protocols/

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
	@rm -rf include/ddk/protocols
	@rm -f ../build/ddk.lib
	@rm -f $(OBJECTS)