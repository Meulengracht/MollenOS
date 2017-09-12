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

LIBK_SRCS = $(wildcard ../libos/mstring/*.c) \
			../libos/synchronization/spinlock.c \
			bufferobject_kernel.c \
			rpc_kernel.c \
			locale/locale.c \
			stdlib/itoa.c \
			stdlib/strtoul.c \
			stdlib/abs.c \
			wstring/wcsnlen.c

LIBC_SRCS = $(wildcard glue/*.c) \
			$(wildcard ctype/*.c) \
			$(wildcard getop/*.c) \
			$(wildcard locale/*.c) \
			$(wildcard signal/*.c) \
			$(wildcard stdio/*.c) \
			$(wildcard stdlib/*.c) \
			$(wildcard stdlib/mb/*.c) \
			$(wildcard stdlib/wide/*.c) \
			$(wildcard time/*.c) \
			$(wildcard wstring/*.c) \
			../libos/synchronization/spinlock.c \
			../libos/driver/bufferobject.c \
			../libos/syscalls.c \
			../libos/pipe.c \
			../libos/rpc.c

# Setup flags and stuff each for individual build  $(subst ../,,$(ASM_SRCS))
ifeq ($(MAKECMDGOALS),libk)
	OBJECTS = $(join $(addprefix obk/, $(dir $(ASM_SRCS))), $(notdir $(ASM_SRCS:.s=.o))) \
			  $(join $(addprefix obk/, $(dir $(COMMON_SRCS))), $(notdir $(COMMON_SRCS:.c=.o))) \
			  $(join $(addprefix obk/, $(dir $(LIBK_SRCS))), $(notdir $(LIBK_SRCS:.c=.o)))
	CFLAGS = $(GCFLAGS) -DLIBC_KERNEL -D_KRNL_DLL $(COMMON_INCS) $(LIBK_INCS)
	LFLAGS = /lib
	OBJDIR = obk
else
	OBJECTS = $(join $(addprefix obc/, $(dir $(ASM_SRCS))), $(notdir $(ASM_SRCS:.s=.o))) \
			  $(join $(addprefix obc/, $(dir $(COMMON_SRCS))), $(notdir $(COMMON_SRCS:.c=.o))) \
			  $(join $(addprefix obc/, $(dir $(LIBC_SRCS))), $(notdir $(LIBC_SRCS:.c=.o)))
	CFLAGS = $(GCFLAGS) -D_HAVE_LONG_DOUBLE -D_LDBL_EQ_DBL -D_DLL -DCRTDLL $(COMMON_INCS)
	LFLAGS = /nodefaultlib /subsystem:native /def:libc.def /noentry /dll ../build/libcrt.lib ../build/libm.lib ../build/libds.lib
	OBJDIR = obc
endif

# libk-target
.PHONY: libk
libk: rpc_kernel.c bufferobject_kernel.c ../build/libk.lib

# libk-dependency on its own version
rpc_kernel.c: ../libos/rpc.c
	cp $< $@

bufferobject_kernel.c: ../libos/driver/bufferobject.c
	cp $< $@

# libc-target
.PHONY: libc
libc: ../build/libc.dll

../build/libk.lib: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../build/libc.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

$(OBJDIR)/%.o : %.c
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) -o $@ $<

$(OBJDIR)/%.o : %.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -f rpc_kernel.c
	rm -f bufferobject_kernel.c
	rm -f ../build/libk.lib
	rm -f ../build/libc.dll
	rm -f ../build/libc.lib
	rm -rf obk
	rm -rf obc
	rm -rf libos