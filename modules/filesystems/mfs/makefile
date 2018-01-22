# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers
SOURCES = $(wildcard ./*.c)

INCLUDES = -I../../../librt/include
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /noentry /dll /lldmap ../../../librt/build/libclang.lib ../../../librt/build/libc.lib ../../../librt/build/libdrv.lib

.PHONY: all
all: ../../build/mfs.dll ../../build/mfs.mdrv

../../build/mfs.dll: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../../build/mfs.mdrv: mfs.mdrv
	@printf "%b" "\033[1;35mCopying settings file " $< "\033[m\n"
	@cp $< $@

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../../build/mfs.dll
	@rm -f ../../build/mfs.lib
	@rm -f ../../build/mfs.mdrv
	@rm -f $(OBJECTS)