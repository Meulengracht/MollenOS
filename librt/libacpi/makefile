# Makefile for building the mollenos kernel image
# Includes sub-libraries as
# - acpica
# - arch sub-layer

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../../kernel/include -I../include -Iinclude
SOURCES = $(wildcard common/*.c) \
		  $(wildcard debugger/*.c) \
		  $(wildcard disassembler/*.c) \
		  $(wildcard dispatcher/*.c) \
		  $(wildcard events/*.c) \
		  $(wildcard executer/*.c) \
		  $(wildcard hardware/*.c) \
		  $(wildcard namespace/*.c) \
		  $(wildcard parser/*.c) \
		  $(wildcard resources/*.c) \
		  $(wildcard tables/*.c) \
		  $(wildcard utilities/*.c) \
		  $(wildcard oslayer/*.c)
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -mno-sse -Wno-unused-const-variable -D_KAPI -DACPI_LIBRARY -DLIBC_KERNEL $(INCLUDES)
LFLAGS = /lib

.PHONY: all
all: ../build/libacpi.lib

../build/libacpi.lib: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../build/libacpi.lib
	@rm -f $(OBJECTS)