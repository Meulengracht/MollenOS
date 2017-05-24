# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers
SOURCES = $(wildcard ./*.c)

INCLUDES = -I../../../../librt/include
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -D_DLL -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:_mDrvCrt /dll ../../../../librt/build/libc.lib ../../../../librt/build/libdrv.lib ../../../../librt/build/libos.lib

.PHONY: all
all: ../../../build/cmos.dll ../../../build/cmos.mdrv

../../../build/cmos.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../../../build/cmos.mdrv: cmos.mdrv
	cp $< $@

%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f ../../../build/cmos.dll
	rm -f ../../../build/cmos.lib
	rm -f ../../../build/cmos.mdrv
	rm -f $(OBJECTS)