# Makefile for building the mollenos kernel image
# Includes sub-libraries as
# - acpica
# - arch sub-layer
INCLUDES = -I../../librt/include -I../include -Iinclude
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

CFLAGS = $(GCFLAGS) -Wno-unused-const-variable -mno-sse -D_KAPI -DACPI_LIBRARY -DLIBC_KERNEL $(INCLUDES)
LFLAGS = /lib

.PHONY: all
all: ../build/acpica.lib

../build/acpica.lib: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f ../build/acpica.lib
	rm -f $(OBJECTS)