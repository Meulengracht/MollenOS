# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers
SOURCES = $(wildcard ./*.c)

INCLUDES = -I../../../../librt/include
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -D_DLL -D__DRIVER_IMPL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /entry:_mDrvCrt /dll ../../../../librt/build/libc.lib ../../../../librt/build/libdrv.lib ../../../../librt/build/libos.lib

.PHONY: all
all: ../../../build/pit.dll ../../../build/pit.mdrv

../../../build/pit.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../../../build/pit.mdrv: pit.mdrv
	cp $< $@

%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f ../../../build/pit.dll
	rm -f ../../../build/pit.lib
	rm -f ../../../build/pit.mdrv
	rm -f $(OBJECTS)