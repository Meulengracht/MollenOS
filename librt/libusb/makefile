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
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.c
	@mkdir -p $(dir $@)
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

%.o : %.s
	@mkdir -p $(dir $@)
	@printf "%b" "\033[0;32mAssembling source object " $< "\033[m\n"
	@$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	@rm -f ../build/libusb.lib
	@rm -f $(OBJECTS)