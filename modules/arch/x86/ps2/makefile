# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers
SOURCES = $(wildcard devices/scancodes/*.c) \
		  $(wildcard devices/*.c) \
		  $(wildcard ./*.c)

INCLUDES = -I../../../../librt/include
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -Wno-invalid-source-encoding -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:__CrtModuleEntry /lldmap /dll ../../../../librt/build/libc.lib ../../../../librt/build/libdrv.lib

.PHONY: all
all: ../../../build/ps2.dll ../../../build/ps2.mdrv

../../../build/ps2.dll: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../../../build/ps2.mdrv: ps2.mdrv
	@printf "%b" "\033[1;35mCopying settings file " $< "\033[m\n"
	@cp $< $@

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../../../build/ps2.dll
	@rm -f ../../../build/ps2.lib
	@rm -f ../../../build/ps2.mdrv
	@rm -f $(OBJECTS)