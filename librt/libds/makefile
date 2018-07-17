# Makefile for building the standard c-libraries for both userspace and kernel
# - libds
# - libdsk

# Include all the definitions for os
include ../../config/common.mk

# Setup architecture specific flags and paths
ifeq ($(VALI_ARCH), i386)
	KERNEL_INCLUDES = -I../../kernel/include \
				      -I../../kernel/arch/x86/x32
else ifeq ($(VALI_ARCH), amd64)
	KERNEL_INCLUDES = -I../../kernel/include \
				      -I../../kernel/arch/x86/x64
else
$(error VALI_ARCH is not set to a valid value)
endif

COMMON_INCLUDES = -I../include
COMMON_SOURCES = $(wildcard *.c)

KERNEL_SOURCES = $(COMMON_SOURCES) support/dsk.c
KERNEL_OBJECTS = $(KERNEL_SOURCES:.c=.ko)

SOURCES = $(COMMON_SOURCES) support/ds.c
OBJECTS = $(SOURCES:.c=.o)

# Setup flags and stuff each for individual build  $(subst ../,,$(ASM_SRCS))
ifeq ($(MAKECMDGOALS), libdsk)
	CFLAGS = $(GCFLAGS) -mno-sse -D_KRNL_DLL $(COMMON_INCLUDES) $(KERNEL_INCLUDES)
else
	CFLAGS = $(GCFLAGS) -D_KRNL_DLL $(COMMON_INCLUDES)
endif

LFLAGS = /lib

# default-target
.PHONY: libdsk
libdsk: ../build/libdsk.lib

.PHONY: libds
libds: ../build/libds.lib

../build/libds.lib: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../build/libdsk.lib: $(KERNEL_OBJECTS)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(KERNEL_OBJECTS) /out:$@

%.o : %.c
	@mkdir -p $(dir $@)
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

%.ko : %.c
	@mkdir -p $(dir $@)
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../build/libds.lib
	@rm -f ../build/libdsk.lib
	@rm -f $(KERNEL_OBJECTS)
	@rm -f $(OBJECTS)