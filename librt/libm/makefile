# Makefile for building the standard c-library math for userspace
#

# Include all the definitions for os
include ../../config/common.mk

ASM_SRCS = $(wildcard $(VALI_ARCH)/*.s)
ARCH_SRCS = $(wildcard $(VALI_ARCH)/*.c)
SOURCES = $(wildcard *.c)
INCLUDES = -I../include

# Setup architecture specific flags and paths
# On windows-i386, double is 32 bits, long double is 64 bits
ifeq ($(VALI_ARCH), i386)
	math_flags = -D_DOUBLE_IS_32BITS -D_HAVE_LONG_DOUBLE -D_LDBL_EQ_DBL
	ASFLAGS = -f win32 -Xvc
	SOURCES += ld80/lgammal_r.c ld80/tgammal.c ld80/expl.c ld80/powl.c ld80/core_cosl.c ld80/core_sinl.c ld80/core_tanl.c ld80/sinl.c
else ifeq ($(VALI_ARCH), amd64)
	math_flags = -D_HAVE_LONG_DOUBLE -D_LDBL_EQ_DBL
	ASFLAGS = -f win64 -Xvc -D_MICROSOFT_LIBM
	SOURCES += ld80/lgammal_r.c ld80/tgammal.c ld80/expl.c ld80/powl.c ld80/core_cosl.c ld80/core_sinl.c ld80/core_tanl.c ld80/logl.c ld80/log10l.c
else
$(error VALI_ARCH is not set to a valid value)
endif

OBJECTS = $(ASM_SRCS:.s=.o) $(ARCH_SRCS:.c=.o) $(SOURCES:.c=.o)
CFLAGS = $(GCFLAGS) -D__OSLIB_C_IMPLEMENTATION -D__OSLIB_C_SHAREDLIBRARY $(math_flags) -DCRTDLL $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap /def:$(VALI_ARCH)/libm.def /noentry /dll 

.PHONY: all
all: ../build/libm.dll

../build/libm.dll: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

%.o : %.s
	@printf "%b" "\033[0;32mAssembling source object " $< "\033[m\n"
	@$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	@rm -f ../build/libm.dll
	@rm -f ../build/libm.lib
	@rm -f $(OBJECTS)