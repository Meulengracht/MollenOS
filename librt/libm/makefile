# Makefile for building the standard c-library math for userspace
#

ASM_SRCS = $(wildcard $(VALI_ARCH)/*.s)
ARCH_SRCS = $(wildcard $(VALI_ARCH)/*.c)
SOURCES = $(wildcard *.c)
INCLUDES = -I../include

# Setup architecture specific flags and paths
ifeq ($(VALI_ARCH), i386)
	math_flags = -D_HAVE_LONG_DOUBLE -D_LDBL_EQ_DBL
	ASFLAGS = -f win32 -Xvc
else ifeq ($(VALI_ARCH), amd64)
	math_flags = -D_HAVE_LONG_DOUBLE
	ASFLAGS = -f win64 -Xvc
else
$(error VALI_ARCH is not set to a valid value)
endif

OBJECTS = $(ASM_SRCS:.s=.o) $(ARCH_SRCS:.c=.o) $(SOURCES:.c=.o)
CFLAGS = $(GCFLAGS) -D__OSLIB_C_IMPLEMENTATION -D__OSLIB_C_SHAREDLIBRARY $(math_flags) -DCRTDLL $(INCLUDES)
LFLAGS = $(GLFLAGS) /def:libm.def /noentry /dll 

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