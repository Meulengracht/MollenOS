# Makefile for building the standard c-libraries for both userspace and kernel
# - libk
# - libc
ASM_SRCS = $(wildcard arch/$(VALI_ARCH)/*.s)

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
       		  $(wildcard stdio/common/*.c)

COMMON_INCS = -I../include

LIBK_SRCS = $(wildcard os/mstring/*.c) \
			os/synchronization/spinlock.c \
			bufferobject_kernel.c \
			rpc_kernel.c \
			locale/locale.c \
			stdlib/itoa.c \
			stdlib/strtoul.c \
			stdlib/abs.c \
			wstring/wcsnlen.c

LIBC_SRCS = $(wildcard ctype/*.c) \
			$(wildcard getop/*.c) \
			$(wildcard locale/*.c) \
			$(wildcard signal/*.c) \
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

# Setup flags and stuff each for individual build  $(subst ../,,$(ASM_SRCS))
ifeq ($(MAKECMDGOALS),libk)
	OBJECTS = $(join $(addprefix obk/, $(dir $(ASM_SRCS))), $(notdir $(ASM_SRCS:.s=.o))) \
			  $(join $(addprefix obk/, $(dir $(COMMON_SRCS))), $(notdir $(COMMON_SRCS:.c=.o))) \
			  $(join $(addprefix obk/, $(dir $(LIBK_SRCS))), $(notdir $(LIBK_SRCS:.c=.o)))
	CFLAGS = $(GCFLAGS) -D__OSLIB_C_IMPLEMENTATION -DLIBC_KERNEL -D_KRNL_DLL -mno-sse $(COMMON_INCS) $(LIBK_INCS)
	LFLAGS = /lib
	OBJDIR = obk
else
	OBJECTS = $(join $(addprefix obc/, $(dir $(ASM_SRCS))), $(notdir $(ASM_SRCS:.s=.o))) \
			  $(join $(addprefix obc/, $(dir $(COMMON_SRCS))), $(notdir $(COMMON_SRCS:.c=.o))) \
			  $(join $(addprefix obc/, $(dir $(LIBC_SRCS))), $(notdir $(LIBC_SRCS:.c=.o)))
	CFLAGS = $(GCFLAGS) -D__OSLIB_C_IMPLEMENTATION -D__OSLIB_C_SHAREDLIBRARY $(math_flags) -DCRTDLL $(COMMON_INCS)
	LFLAGS = $(GLFLAGS) /def:libc.def /dll /entry:__CrtLibraryEntry /lldmap ../build/libcrt.lib ../build/libclang.lib ../build/libm.lib ../build/libds.lib
	OBJDIR = obc
endif

# libk-target
.PHONY: libk
libk: rpc_kernel.c bufferobject_kernel.c ../build/libk.lib

# libk-dependency on its own version
rpc_kernel.c: os/rpc.c
	cp $< $@

bufferobject_kernel.c: os/driver/bufferobject.c
	cp $< $@

# libc-target
.PHONY: libc
libc: ../build/libc.dll

../build/libk.lib: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../build/libc.dll: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@

$(OBJDIR)/%.o : %.c
	@mkdir -p $(dir $@)
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

$(OBJDIR)/%.o : %.s
	@mkdir -p $(dir $@)
	@printf "%b" "\033[0;32mAssembling source object " $< "\033[m\n"
	@$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	@rm -f rpc_kernel.c
	@rm -f bufferobject_kernel.c
	@rm -f ../build/libk.lib
	@rm -f ../build/libc.dll
	@rm -f ../build/libc.lib
	@rm -rf obk
	@rm -rf obc
	@rm -rf libos