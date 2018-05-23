# Makefile for building the mollenos kernel image
# Includes sub-libraries as
# - acpica
# - arch sub-layer
INCLUDES = -I../../librt/include -I../../librt/libacpi/include -I../include

ifeq ($(VALI_ARCH),i386)
    INCLUDES += -Ix86 -Ix86/x32
    SOURCES = $(wildcard x86/components/*.c) \
			  $(wildcard x86/interrupts/apic/*.c) \
			  $(wildcard x86/interrupts/pic/*.c) \
			  $(wildcard x86/interrupts/*.c) \
			  $(wildcard x86/x32/memory/*.c) \
			  $(wildcard x86/x32/*.c) \
			  $(wildcard x86/*.c)
    ASSEMBLY = $(wildcard x86/x32/memory/*.s) \
			   $(wildcard x86/x32/*.s)
	ASFLAGS = -f win32 -Xvc
	HPATH = x86/x32/arch.h
else ifeq ($(VALI_ARCH),amd64)
	INCLUDES += -Ix86 -Ix86/x64
	SOURCES = $(wildcard x86/components/*.c) \
			  $(wildcard x86/interrupts/apic/*.c) \
			  $(wildcard x86/interrupts/pic/*.c) \
			  $(wildcard x86/interrupts/*.c) \
			  $(wildcard x86/x64/memory/*.c) \
			  $(wildcard x86/x64/*.c) \
			  $(wildcard x86/*.c)
    ASSEMBLY = $(wildcard x86/x64/memory/*.s) \
			   $(wildcard x86/x64/*.s)
	ASFLAGS = -f win64 -Xvc
	HPATH = x86/x64/arch.h
else
$(error VALI_ARCH is not set to a valid value)
endif

OBJECTS = $(ASSEMBLY:.s=.o) $(SOURCES:.c=.o)
CFLAGS = $(GCFLAGS) -mno-sse -D_KAPI -DLIBC_KERNEL -DUNICODE -Wno-address-of-packed-member $(INCLUDES)
LFLAGS = /lib

.PHONY: all
all: header ../build/$(VALI_ARCH).lib

.PHONY: header
header:
	cp $(HPATH) ../include/arch.h

../build/$(VALI_ARCH).lib: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

%.o : %.s
	@printf "%b" "\033[0;32mAssembling source object " $< "\033[m\n"
	@$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	@rm -f ../include/arch.h
	@rm -f ../build/$(VALI_ARCH).lib
	@rm -f $(OBJECTS)