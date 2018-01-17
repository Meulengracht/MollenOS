# Makefile for building the mollenos kernel image
# Includes sub-libraries as
# - acpica
# - arch sub-layer
INCLUDES = -I../librt/include -Iacpica/include -Iinclude
SOURCES = $(wildcard common/*.c) \
		  $(wildcard system/acpi/*.c) \
		  $(wildcard system/output/fonts/*.c) \
		  $(wildcard system/output/*.c) \
		  $(wildcard system/process/loaders/*.c) \
		  $(wildcard system/process/*.c) \
		  $(wildcard system/synchronization/*.c) \
		  $(wildcard system/*.c) \
		  init.c
OBJECTS = $(SOURCES:.c=.o)

# --save-temps
CFLAGS = $(GCFLAGS) -D_KAPI -DUNICODE -DLIBC_KERNEL -D_KRNL_DLL -mno-sse $(INCLUDES)
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
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	$(MAKE) -C acpica -f makefile clean
	$(MAKE) -C arch -f makefile clean
	rm -f $(OBJECTS)
	rm -rf build