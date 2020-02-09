# Makefile for building the standard c-library math for userspace
#

# Include all the definitions for os
include ../../config/common.mk

ASM_SRCS = $(wildcard $(VALI_ARCH)/*.s)
ARCH_SRCS = $(wildcard $(VALI_ARCH)/*.c)
SOURCES = $(wildcard *.c)
INCLUDES = -Iinclude -I../include -I../libc/include

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

SHARED_OBJECTS = $(ASM_SRCS:.s=.o) $(ARCH_SRCS:.c=.s_o) $(SOURCES:.c=.s_o)
SHARED_CFLAGS = $(GCFLAGS) -fno-builtin -fno-strict-aliasing -D__OSLIB_C_IMPLEMENTATION -D__OSLIB_C_SHAREDLIBRARY $(math_flags) -DCRTDLL $(INCLUDES)
SHARED_LFLAGS = $(GLFLAGS) /lldmap /def:$(VALI_ARCH)/libm.def /noentry /dll 

STATIC_OBJECTS = $(ASM_SRCS:.s=.o) $(ARCH_SRCS:.c=.o) $(SOURCES:.c=.o)
STATIC_CFLAGS = $(GCFLAGS) -fno-builtin -fno-strict-aliasing $(math_flags) $(INCLUDES)
STATIC_LFLAGS = /lib

.PHONY: all
all: ../build/static_m.lib ../deploy/m.dll

../build/static_m.lib: $(STATIC_OBJECTS)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(STATIC_LFLAGS) $(STATIC_OBJECTS) /out:$@

../deploy/m.dll: $(SHARED_OBJECTS)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(SHARED_LFLAGS) $(SHARED_OBJECTS) /out:$@

%.s_o : %.c
	@printf "%b" "\033[0;32m[LIBM] Compiling C source object " $< "\033[m\n"
	@$(CC) -c $(SHARED_CFLAGS) -o $@ $<

%.o : %.c
	@printf "%b" "\033[0;32m[LIBM] Compiling C source object " $< "\033[m\n"
	@$(CC) -c $(STATIC_CFLAGS) -o $@ $<

%.o : %.s
	@printf "%b" "\033[0;32m[LIBM] Assembling source object " $< "\033[m\n"
	@$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	@rm -f ../build/static_m.lib
	@rm -f ../deploy/m.lib
	@rm -f ../deploy/m.lib
	@rm -f $(STATIC_OBJECTS) $(SHARED_OBJECTS)