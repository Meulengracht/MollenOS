# Makefile for building the mollenos kernel image
# Includes sub-libraries as
# - acpica
# - arch sub-layer

# Include all the definitions for os
include ../config/common.mk

# We include some out of tree sources that are 'shared' across stuff, like
# the some of the ddk sources that has utils we need around
OOT_SOURCES = ../librt/libddk/io.c

# Get all the neccessary include paths, pretty much all in the project
INCLUDES = -Iinclude -Iarch/include -I../librt/libds/include \
		   -I../librt/libacpi/source/include -I../librt/libddk/include \
		   -I../librt/libc/include -I../librt/include

# All the project sources
SOURCES = $(wildcard acpi/*.c) \
		  $(wildcard components/*.c) \
		  $(wildcard memory/*.c) \
		  $(wildcard modules/*.c) \
		  $(wildcard output/fonts/*.c) \
		  $(wildcard output/*.c) \
		  $(wildcard scheduling/*.c) \
		  $(wildcard system_calls/*.c) \
		  $(wildcard tests/*.c) \
		  $(wildcard utils/*.c) \
		  $(wildcard *.c) \
		  $(OOT_SOURCES)
OBJECTS = $(SOURCES:.c=.o)
PVSINTS = $(SOURCES:.c=.o.PVS-Studio.i)
PVSLOGS = $(SOURCES:.c=.o.PVS-Studio.log)

# Setup dependencies for the kernel object
DEPENDENCIES = ../librt/build/crt.lib ../librt/build/compiler-rt.lib ../librt/build/libk.lib ../librt/build/libdsk.lib ../librt/build/libacpi.lib build/$(VALI_ARCH).lib

# --save-temps
CFLAGS = $(GCFLAGS) -mno-sse -DUNICODE -DLIBC_KERNEL -D_KRNL_DLL -D__LIBDS_KERNEL__ $(INCLUDES)
LFLAGS = $(GLFLAGS) /entry:kentry /fixed /base:"0x100000" /manifest:no /lldmap $(DEPENDENCIES)
PVSFLAGS = --skip-cl-exe=yes --preprocessor=clang --platform=$(PVS_PLATFORM) --analysis-mode=4 --language=C

.PHONY: all
all: build arch build/syskrnl.mos

build:
	@mkdir -p $@

.PHONY: arch
arch:
	$(MAKE) -C arch -f makefile

build/syskrnl.mos: $(OBJECTS) $(DEPENDENCIES)
	@printf "%b" "\033[0;36mCreating kernel executable " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@
ifeq ($(VALI_PVS),true)
	plog-converter -a 'GA:1,2' -t $(PVS_FORMAT) $(PVSLOGS) -o $@.html
endif
	
%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<
ifeq ($(VALI_PVS),true)
	@$(CC) -c $(CFLAGS) $< -E -o $@.PVS-Studio.i
	@pvs-studio $(PVSFLAGS) --source-file $< --i-file $@.PVS-Studio.i --output-file $@.PVS-Studio.log
endif

.PHONY: clean
clean:
	@$(MAKE) -s -C arch -f makefile clean
ifeq ($(VALI_PVS),true)
	@rm -f $(PVSINTS) $(PVSLOGS)
endif
	@rm -f $(OBJECTS)
	@rm -rf build