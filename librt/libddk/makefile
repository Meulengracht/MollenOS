# Makefile for building the standard c-libraries for both userspace and kernel
# - libddk

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../include -I../libc/include -Iinclude

# Setup architecture specific flags and paths
ifeq ($(VALI_ARCH), i386)
	ASFLAGS = -f win32 -Xvc
else ifeq ($(VALI_ARCH), amd64)
	ASFLAGS = -f win64 -Xvc
else
$(error VALI_ARCH is not set to a valid value)
endif

ASM_SOURCES = $(wildcard arch/$(VALI_ARCH)/*.s)
SOURCES = $(wildcard **/*.c) $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o) $(ASM_SOURCES:.s=.o)

# Setup flags
CFLAGS = $(GCFLAGS) $(INCLUDES)
LFLAGS = /lib

# default-target
.PHONY: all
all: ../build/ddk.lib

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
	@rm -f ../build/ddk.lib
	@rm -f $(OBJECTS)