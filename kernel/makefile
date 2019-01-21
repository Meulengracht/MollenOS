# Makefile for building the mollenos kernel image
# Includes sub-libraries as
# - acpica
# - arch sub-layer

# Include all the definitions for os
include ../config/common.mk

INCLUDES = -I../librt/include -I../librt/libc/include -I../librt/libds/include -I../librt/libddk/include -I../librt/libacpi/include -Iinclude
SOURCES = $(wildcard acpi/*.c) \
		  $(wildcard components/*.c) \
		  $(wildcard modules/*.c) \
		  $(wildcard output/fonts/*.c) \
		  $(wildcard output/*.c) \
		  $(wildcard synchronization/*.c) \
		  $(wildcard system_calls/*.c) \
		  $(wildcard tests/*.c) \
		  $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

# Setup dependencies for the kernel object
DEPENDENCIES = ../librt/build/libcrt.lib ../librt/build/libclang.lib ../librt/build/libk.lib ../librt/build/libdsk.lib ../librt/build/libacpi.lib build/$(VALI_ARCH).lib

# --save-temps
CFLAGS = $(GCFLAGS) -mno-sse -D_KAPI -DUNICODE -DLIBC_KERNEL -D_KRNL_DLL $(INCLUDES)
LFLAGS = $(GLFLAGS) /entry:kentry /fixed /base:"0x100000" /manifest:no /lldmap $(DEPENDENCIES)

.PHONY: all
all: build arch build/syskrnl.mos

# Static code analysis targets that do not link, but only compile
.PHONY: tidy
tidy: tidy_arch $(OBJECTS)

.PHONY: tidy_arch
tidy_arch:
	$(MAKE) -C arch -f makefile tidy

build:
	@mkdir -p $@

.PHONY: arch
arch:
	$(MAKE) -C arch -f makefile

build/syskrnl.mos: $(OBJECTS) $(DEPENDENCIES)
	@printf "%b" "\033[0;36mCreating kernel executable " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@$(MAKE) -s -C arch -f makefile clean
	@rm -f $(OBJECTS)
	@rm -rf build