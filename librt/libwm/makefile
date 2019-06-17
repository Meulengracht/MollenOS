# Makefile for building the standard c-libraries for both userspace and kernel
# - libwm

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../include -I../libc/include -I../libddk/include

SOURCES = $(wildcard *.c)
OBJECTS = $(SOURCES:.c=.o)

# Setup flags
CFLAGS = $(GCFLAGS) $(INCLUDES)
LFLAGS = /lib

# default-target
.PHONY: all
all: ../build/libwm.lib

../build/libwm.lib: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.c
	@mkdir -p $(dir $@)
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../build/libwm.lib
	@rm -f $(OBJECTS)