# Makefile for building all run-time and support libraries
ASM_SRCS = $(wildcard arch/$(arch)/*.s) 
ASFLAGS = -f win32 -Xvc

SOURCES = $(wildcard driver/*.c) \
		  $(wildcard common/*.c) \
      	  $(wildcard ds/*.c) \
       	  $(wildcard mstring/*.c) \
       	  $(wildcard synchronization/*.c) \
       	  $(wildcard thread/*.c) \
       	  $(wildcard ./*.c)

INCLUDES = -I../include
OBJECTS = $(ASM_SRCS:.s=.o) $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -D_DLL -D_LIBOS_DLL $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /noentry /dll ../build/libc.lib ../build/libcrt.lib

.PHONY: all
all: ../build/libos.dll

../build/libos.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

%.o : %.s
	$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -f ../build/libos.dll
	rm -f ../build/libos.lib
	rm -f $(OBJECTS)