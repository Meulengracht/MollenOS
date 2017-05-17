# Makefile for building the standard c-library math for userspace
#

ASM_SRCS = $(wildcard $(arch)/*.s)
ARCH_SRCS = $(wildcard $(arch)/*.c)
SOURCES = $(wildcard *.c)
INCLUDES = -I../include

OBJECTS = $(ASM_SRCS:.s=.o) $(ARCH_SRCS:.c=.o) $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /noentry /dll
ASFLAGS = -f win32 -Xvc

.PHONY: all
all: ../build/libm.dll

../build/libm.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

%.o : %.s
	$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	@rm -f ../build/libm.dll
	rm -f $(OBJECTS)