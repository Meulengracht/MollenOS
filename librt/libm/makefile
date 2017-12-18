# Makefile for building the standard c-library math for userspace
#

ASM_SRCS = $(wildcard $(arch)/*.s)
ARCH_SRCS = $(wildcard $(arch)/*.c)
SOURCES = $(wildcard *.c)
INCLUDES = -I../include

OBJECTS = $(ASM_SRCS:.s=.o) $(ARCH_SRCS:.c=.o) $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) -D__OSLIB_C_IMPLEMENTATION -D__OSLIB_C_SHAREDLIBRARY -D_HAVE_LONG_DOUBLE -D_LDBL_EQ_DBL -DCRTDLL $(INCLUDES)
LFLAGS = $(GLFLAGS) /def:libm.def /noentry /dll 
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
	rm -f ../build/libm.dll
	rm -f ../build/libm.lib
	rm -f $(OBJECTS)