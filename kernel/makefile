# Makefile for building the mollenos kernel image
# Includes sub-libraries as
# - acpica
# - arch sub-layer
INCLUDES = -I../librt/include -Iinclude
SOURCES = $(wildcard common/*.c) \
		  $(wildcard system/devices/acpi/*.c) \
		  $(wildcard system/devices/fonts/*.c) \
		  $(wildcard system/devices/*.c) \
		  $(wildcard system/process/loaders/*.c) \
		  $(wildcard system/process/*.c) \
		  $(wildcard system/synchronization/*.c) \
		  $(wildcard system/*.c) \
		  init.c
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:_kentry build/acpica.lib build/$(arch).lib

.PHONY: all
all: build acpica arch build/mcore.mos

build:
	@mkdir -p $@

acpica:
	$(MAKE) -C acpica -f makefile

arch:
	$(MAKE) -C arch -f makefile

build/mcore.mos: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	$(MAKE) -C acpica -f makefile clean
	$(MAKE) -C arch -f makefile clean
	rm -f $(OBJECTS)
	rm -rf build