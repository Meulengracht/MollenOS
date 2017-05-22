# Makefile for building the mollenos kernel image
# Includes sub-libraries as
# - acpica
# - arch sub-layer
INCLUDES = -I../../librt/include -I../acpica/include -I../include

ifeq ($(arch),i386)
    INCLUDES += -Ix86 -Ix86/x32
    SOURCES = $(wildcard x86/components/*.c) \
			  $(wildcard x86/interrupts/apic/*.c) \
			  $(wildcard x86/interrupts/pic/*.c) \
			  $(wildcard x86/interrupts/*.c) \
			  $(wildcard x86/x32/memory/*.c) \
			  $(wildcard x86/x32/boot/*.c) \
			  $(wildcard x86/x32/*.c) \
			  $(wildcard x86/*.c)
    ASSEMBLY = $(wildcard x86/components/*.s) \
			   $(wildcard x86/x32/memory/*.s) \
			   $(wildcard x86/x32/boot/*.s) \
			   $(wildcard x86/x32/*.s)
	ASFLAGS = -f win32 -Xvc
	HPATH = x86/x32/arch.h
else

endif

OBJECTS = $(ASSEMBLY:.s=.o) $(SOURCES:.c=.o)
CFLAGS = $(GCFLAGS) -D_KAPI -DLIBC_KERNEL -Wno-address-of-packed-member $(INCLUDES)
LFLAGS = /lib

.PHONY: all
all: header ../build/$(arch).lib

.PHONY: header
header:
	cp $(HPATH) ../include/arch.h

../build/i386.lib: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

%.o : %.s
	$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -f ../include/arch.h
	rm -f ../build/$(arch).lib
	rm -f $(OBJECTS)