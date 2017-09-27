# Makefile for building the standard c-libraries for both userspace and kernel
# - libusb
ASFLAGS = -f win32 -Xvc
INCLUDES = -I../include

SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

# Setup flags
CFLAGS = $(GCFLAGS) -D_KRNL_DLL $(INCLUDES)

LFLAGS = /lib

# default-target
.PHONY: all
all: ../build/libusb.lib

../build/libusb.lib: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) -o $@ $<

%.o : %.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -f ../build/libusb.lib
	@rm -f $(OBJECTS)