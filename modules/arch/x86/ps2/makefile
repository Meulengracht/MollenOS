# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers
SOURCES = $(wildcard devices/scancodes/*.c) \
		  $(wildcard devices/*.c) \
		  $(wildcard ./*.c)

INCLUDES = -I../../../../librt/include
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -Wno-invalid-source-encoding -D_DLL -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:_mDrvCrt /dll ../../../../librt/build/libc.lib ../../../../librt/build/libdrv.lib ../../../../librt/build/libos.lib

.PHONY: all
all: ../../../build/ps2.dll ../../../build/ps2.mdrv

../../../build/ps2.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../../../build/ps2.mdrv: ps2.mdrv
	cp $< $@

%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f ../../../build/ps2.dll
	rm -f ../../../build/ps2.lib
	rm -f ../../../build/ps2.mdrv
	rm -f $(OBJECTS)