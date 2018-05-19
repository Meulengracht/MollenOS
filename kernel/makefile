# Makefile for building the mollenos kernel image
# Includes sub-libraries as
# - acpica
# - arch sub-layer
INCLUDES = -I../librt/include -Iacpica/include -Iinclude
SOURCES = $(wildcard acpi/*.c) \
		  $(wildcard components/*.c) \
		  $(wildcard output/fonts/*.c) \
		  $(wildcard output/*.c) \
		  $(wildcard process/loaders/*.c) \
		  $(wildcard process/*.c) \
		  $(wildcard synchronization/*.c) \
		  $(wildcard *.c) \
		  init.c
OBJECTS = $(SOURCES:.c=.o)

# --save-temps
CFLAGS = $(GCFLAGS) -mno-sse -D_KAPI -DUNICODE -DLIBC_KERNEL -D_KRNL_DLL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:kentry /fixed /base:"0x100000" /manifest:no /lldmap ../librt/build/libcrt.lib ../librt/build/libclang.lib ../librt/build/libk.lib ../librt/build/libdsk.lib build/acpica.lib build/$(VALI_ARCH).lib

.PHONY: all
all: build arch acpica build/syskrnl.mos

build:
	@mkdir -p $@

.PHONY: acpica
acpica:
	$(MAKE) -C acpica -f makefile

.PHONY: arch
arch:
	$(MAKE) -C arch -f makefile

build/syskrnl.mos: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating kernel executable " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@$(MAKE) -s -C acpica -f makefile clean
	@$(MAKE) -s -C arch -f makefile clean
	@rm -f $(OBJECTS)
	@rm -rf build