# Makefile for building the standard c-libraries for both userspace and kernel
# - libds
# - libdsk

# Include all the definitions for os
include ../../config/common.mk

# Setup architecture specific flags and paths
ifeq ($(VALI_ARCH), i386)
	KERNEL_INCLUDES = -I../../kernel/include \
					  -I../../kernel/arch/include \
				      -I../../kernel/arch/x86/x32
else ifeq ($(VALI_ARCH), amd64)
	KERNEL_INCLUDES = -I../../kernel/include \
					  -I../../kernel/arch/include \
				      -I../../kernel/arch/x86/x64
else
$(error VALI_ARCH is not set to a valid value)
endif

COMMON_INCLUDES = -I../include -I../libc/include -Iinclude -I../libddk/include
COMMON_SOURCES = $(wildcard mstring/*.c) $(wildcard pe/*.c) $(wildcard *.c)

KERNEL_SOURCES = $(COMMON_SOURCES) support/dsk.c
KERNEL_OBJECTS = $(KERNEL_SOURCES:.c=.ko)

SOURCES = $(COMMON_SOURCES) support/ds.c
OBJECTS = $(SOURCES:.c=.o)

# Setup flags and stuff each for individual build  $(subst ../,,$(ASM_SRCS))
KERNEL_CFLAGS = $(GCFLAGS) -mno-sse -D_KRNL_DLL $(COMMON_INCLUDES) $(KERNEL_INCLUDES)
NORMAL_CFLAGS = $(GCFLAGS) $(COMMON_INCLUDES)
LFLAGS = /lib

all: ../build/libds.lib ../build/libdsk.lib

# tidy-target
.PHONY: tidy
tidy: $(KERNEL_OBJECTS) $(OBJECTS)

../build/libds.lib: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../build/libdsk.lib: $(KERNEL_OBJECTS)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(KERNEL_OBJECTS) /out:$@

%.o : %.c
	@mkdir -p $(dir $@)
	@printf "%b" "\033[0;32m[LIBDS] Compiling C source object " $< "\033[m\n"
	@$(CC) -c $(NORMAL_CFLAGS) -o $@ $<

%.ko : %.c
	@mkdir -p $(dir $@)
	@printf "%b" "\033[0;32m[LIBDS] Compiling C source object " $< "\033[m\n"
	@$(CC) -c $(KERNEL_CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../build/libds.lib
	@rm -f ../build/libdsk.lib
	@rm -f $(KERNEL_OBJECTS)
	@rm -f $(OBJECTS)