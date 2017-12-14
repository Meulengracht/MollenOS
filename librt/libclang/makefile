# Makefile for building the standard c-library math for userspace
#

ASM_SRCS = $(wildcard lib/builtins/$(arch)/*.S)
SOURCES = $(wildcard lib/builtins/*.c)
INCLUDES = -I../include

OBJECTS = $(ASM_SRCS:.S=.o) $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -D_HAVE_LONG_DOUBLE -D_LDBL_EQ_DBL -DCRTDLL $(INCLUDES)
LFLAGS = /lib

.PHONY: all
all: ../build/libclang.lib

../build/libclang.lib: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

%.o : %.S
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f ../build/libclang.lib
	rm -f $(OBJECTS)