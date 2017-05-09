# Makefile for building the standard c-libraries for both userspace and kernel
# - libk
# - libc
ASM_SRCS = $(wildcard arch/$(arch)/*.s) \
		   $(wildcard ../libos/arch/$(arch)/*.s)

COMMON_SRCS = $(wildcard assert/*.c) \
      		  $(wildcard errno/*.c) \
       		  $(wildcard mem/*.c) \
       		  $(wildcard string/*.c) \
       		  $(wildcard ctype/common/*.c) \
       		  $(wildcard stdio/common/*.c)

LIBK_SRCS = $(wildcard ../libos/ds/*.c) \
			$(wildcard ../libos/mstring/*.c) \
			locale/locale.c \
			stdlib/itoa.c \
			stdlib/strtoul.c
LIBC_SRCS = 

LIBK_OBJS = $(ASM_SRCS:.s=.o) $(COMMON_SRCS:.c=.o) $(LIBK_SRCS:.c=.o)
LIBC_OBJS = $(ASM_SRCS:.s=.o) $(COMMON_SRCS:.c=.o) $(LIBC_SRCS:.c=.o)

CFLAGS = -Wall $(DEBUG)
LFLAGS = /nodefaultlib /subsystem:native /entry:_kmain

.PHONY: all
all: ../build/libk.lib ../build/libc.dll

.PHONY: libk
libk: ../build/libk.lib

.PHONY: libc
libc: ../build/libc.dll

../build/libk.lib: $(LIBK_OBJS)
	$(LD) $(LFLAGS) $(LIBK_OBJS) -o $@

../build/libc.dll: $(LIBC_OBJS)
	$(LD) $(LFLAGS) $(LIBC_OBJS) -o $@

%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

%.o : %.s
	$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
	rm $(LIBC_OBJS) $(LIBK_OBJS)