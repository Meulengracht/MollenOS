# Makefile for building the standard c-libraries for both userspace and kernel
# - libds
# - libdsk
ASM_SRCS = $(wildcard arch/$(arch)/*.s) \
		   $(wildcard ../libos/arch/$(arch)/*.s)

ASFLAGS = -f win32 -Xvc
COMMON_INCLUDES = -I../include
COMMON_SOURCES = $(wildcard *.c)

KERNEL_INCLUDES = -I../../kernel/include \
				  -I../../kernel/arch/x86/x32
KERNEL_SOURCES = $(COMMON_SOURCES) support/dsk.c
KERNEL_OBJECTS = $(KERNEL_SOURCES:.c=.o)

SOURCES = $(COMMON_SOURCES) support/ds.c
OBJECTS = $(SOURCES:.c=.o)

# Setup flags and stuff each for individual build  $(subst ../,,$(ASM_SRCS))
ifeq ($(MAKECMDGOALS), libdsk)
	CFLAGS = $(GCFLAGS) -D_KRNL_DLL -mno-sse $(COMMON_INCLUDES) $(KERNEL_INCLUDES)
else
	CFLAGS = $(GCFLAGS) -D_KRNL_DLL $(COMMON_INCLUDES)
endif

LFLAGS = /lib

# default-target
.PHONY: libdsk
libdsk: ../build/libdsk.lib

.PHONY: libds
libdsk: ../build/libds.lib

../build/libds.lib: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../build/libdsk.lib: $(KERNEL_OBJECTS)
	$(LD) $(LFLAGS) $(KERNEL_OBJECTS) /out:$@

%.o : %.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) -o $@ $<

%.o : %.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -f ../build/libds.lib
	rm -f ../build/libdsk.lib
	@rm -f $(KERNEL_OBJECTS)
	@rm -f $(OBJECTS)