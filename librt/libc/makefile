# Makefile for building the standard c-libraries for both userspace and kernel
# - libk
# - libc
ASM_SRCS = $(wildcard arch/$(VALI_ARCH)/*.s)

# Include all the definitions for os
include ../../config/common.mk

# Setup architecture specific flags and paths
ifeq ($(VALI_ARCH), i386)
	math_flags = -D_HAVE_LONG_DOUBLE -D_LDBL_EQ_DBL
	ASFLAGS = -f win32 -Xvc
	LIBK_INCS = -I../../kernel/include \
			    -I../../kernel/arch/x86/x32
else ifeq ($(VALI_ARCH), amd64)
	math_flags = -D_HAVE_LONG_DOUBLE
	ASFLAGS = -f win64 -Xvc
	LIBK_INCS = -I../../kernel/include \
			    -I../../kernel/arch/x86/x64
else
$(error VALI_ARCH is not set to a valid value)
endif

COMMON_SRCS = $(wildcard assert/*.c) \
      		  $(wildcard errno/*.c) \
       		  $(wildcard mem/*.c) \
       		  $(wildcard string/*.c) \
       		  $(wildcard ctype/common/*.c) \
       		  $(wildcard stdio/common/*.c) \
       		  $(wildcard time/common/*.c)

COMMON_INCS = -I../include -Iinclude -I../libds/include -I../libddk/include

LIBK_SRCS = $(wildcard os/mstring/*.c) \
			os/synchronization/spinlock.c \
			os/rpc.c \
			locale/locale.c \
			stdlib/itoa.c \
			stdlib/strtoul.c \
			stdlib/abs.c \
			wstring/wcsnlen.c

LIBC_SRCS = $(wildcard ctype/*.c) \
			$(wildcard getopt/*.c) \
			$(wildcard locale/*.c) \
			$(wildcard signal/*.c) \
			$(wildcard stdio/keymaps/*.c) \
			$(wildcard stdio/*.c) \
			$(wildcard stdlib/*.c) \
			$(wildcard stdlib/mb/*.c) \
			$(wildcard stdlib/wide/*.c) \
			$(wildcard time/*.c) \
			$(wildcard wstring/*.c) \
			$(wildcard threads/*.c) \
			$(wildcard threads/*.c) \
			$(wildcard os/**/*.c) \
			$(wildcard os/*.c) \
			main.c

LIBK_OBJECTS = $(ASM_SRCS:.s=.ko) $(COMMON_SRCS:.c=.ko) $(LIBK_SRCS:.c=.ko)
LIBC_OBJECTS = $(ASM_SRCS:.s=.o) $(COMMON_SRCS:.c=.o) $(LIBC_SRCS:.c=.o)

LIBC_DEPENDENCIES = ../build/libcrt.lib ../build/libclang.lib ../build/libm.lib ../build/libds.lib

# Setup flags and stuff each for individual build  $(subst ../,,$(ASM_SRCS))
LIBK_CFLAGS = $(GCFLAGS) -mno-sse -D__OSLIB_C_IMPLEMENTATION -DLIBC_KERNEL -D_KRNL_DLL $(COMMON_INCS) $(LIBK_INCS)
LIBK_LFLAGS = /lib

LIBC_CFLAGS = $(GCFLAGS) -D__OSLIB_C_IMPLEMENTATION -D__OSLIB_C_SHAREDLIBRARY $(math_flags) -DCRTDLL $(COMMON_INCS)
LIBC_LFLAGS = $(GLFLAGS) /def:libc.def /dll /entry:__CrtLibraryEntry /lldmap $(LIBC_DEPENDENCIES)

# libk-target
.PHONY: libk
libk: ../build/libk.lib

# libc-target
.PHONY: libc
libc: ../build/libc.dll

# tidy-target
.PHONY: tidy
tidy: $(LIBK_OBJECTS) $(LIBC_OBJECTS)

../build/libk.lib: $(LIBK_OBJECTS)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(LIBK_LFLAGS) $(LIBK_OBJECTS) /out:$@

../build/libc.dll: $(LIBC_OBJECTS) $(LIBC_DEPENDENCIES)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LIBC_LFLAGS) $(LIBC_OBJECTS) /out:$@

%.ko : %.c
	@printf "%b" "\033[0;32m[LIBK] Compiling C source object " $< "\033[m\n"
	@$(CC) -c $(LIBK_CFLAGS) -o $@ $<

%.ko : %.s
	@printf "%b" "\033[0;32m[LIBK] Assembling source object " $< "\033[m\n"
	@$(AS) $(ASFLAGS) $< -o $@

%.o : %.c
	@printf "%b" "\033[0;32m[LIBC] Compiling C source object " $< "\033[m\n"
	@$(CC) -c $(LIBC_CFLAGS) -o $@ $<

%.o : %.s
	@printf "%b" "\033[0;32m[LIBC] Assembling source object " $< "\033[m\n"
	@$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	@rm -f ../build/libk.lib
	@rm -f ../build/libc.dll
	@rm -f ../build/libc.lib
	@rm -rf $(LIBC_OBJECTS)
	@rm -rf $(LIBK_OBJECTS)