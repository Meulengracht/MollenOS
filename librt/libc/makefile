# Makefile for building the standard c-libraries for both userspace and kernel
# - libk
# - libc
ASM_SRCS = $(wildcard arch/$(arch)/*.s) \
		   $(wildcard ../libos/arch/$(arch)/*.s)

ASFLAGS = -f win32 -Xvc

COMMON_SRCS = $(wildcard assert/*.c) \
      		  $(wildcard errno/*.c) \
       		  $(wildcard mem/*.c) \
       		  $(wildcard string/*.c) \
       		  $(wildcard ctype/common/*.c) \
       		  $(wildcard stdio/common/*.c)

COMMON_INCS = -I../include

LIBK_INCS = -I../../kernel/include \
			-I../../kernel/arch/x86/x32

LIBK_SRCS = $(wildcard ../libos/ds/*.c) \
			$(wildcard ../libos/mstring/*.c) \
			locale/locale.c \
			stdlib/itoa.c \
			stdlib/strtoul.c

LIBC_SRCS = 

LIBC_INCS = 

LIBK_OBJS = $(ASM_SRCS:.s=.o) $(COMMON_SRCS:.c=.o) $(LIBK_SRCS:.c=.o)
LIBC_OBJS = $(ASM_SRCS:.s=.o) $(COMMON_SRCS:.c=.o) $(LIBC_SRCS:.c=.o)

# LibK flags - 
libk: CFLAGS = $(GCFLAGS) -DLIBC_KERNEL -D_KRNL_DLL $(COMMON_INCS) $(LIBK_INCS) 
libk: LFLAGS = /lib

# LibC flags
libc: CFLAGS = $(GCFLAGS) -D_HAVE_LONG_DOUBLE -D_LDBL_EQ_DBL -DCRTDLL $(COMMON_INCS)
libc: LFLAGS = /nodefaultlib /subsystem:native /DLL

.PHONY: all
all: ../build/libk.lib ../build/libc.dll

.PHONY: libk
libk: ../build/libk.lib

.PHONY: libc
libc: ../build/libc.dll

../build/libk.lib: $(LIBK_OBJS)
	$(LD) $(LFLAGS) $(LIBK_OBJS) /out:$@

../build/libc.dll: $(LIBC_OBJS)
	$(LD) $(LFLAGS) $(LIBC_OBJS) /out:$@

%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

%.o : %.s
	$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -f ../build/libk.lib
	rm -f ../build/libc.dll
	rm -f $(LIBC_OBJS) $(LIBK_OBJS)