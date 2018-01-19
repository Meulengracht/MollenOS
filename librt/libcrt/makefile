# Makefile for building the standard c runtime-libraries for userspace
# This will produce the file libcrt.lib, libsrv.lib, libdrv.lib

INCLUDES = -I../include

C_SRCS_ALL = $(wildcard vscrt/*.c) \
			 $(wildcard ./*.c)
OBJECTS_ALL = $(C_SRCS_ALL:.c=.o)

ifeq ($(MAKECMDGOALS),libcrt)
	SOURCES = entry.c crtcoff.c library.c
	CFLAGS = $(GCFLAGS) $(INCLUDES)
else 
	ifeq ($(MAKECMDGOALS),libdrv)
		SOURCES = module.c crtcoff.c
		CFLAGS = $(GCFLAGS) -D__DRIVER_IMPL $(INCLUDES)
	else
		SOURCES = service.c crtcoff.c
		CFLAGS = $(GCFLAGS) -D__SERVICE_IMPL $(INCLUDES)
	endif
endif

OBJECTS = $(ASM_SRCS:.s=.o) $(SOURCES:.c=.o)
LFLAGS = /lib

# default-target
.PHONY: libcrt
libcrt: ../build/libcrt.lib

.PHONY: libdrv
libdrv: ../build/libdrv.lib

.PHONY: libsrv
libsrv: ../build/libsrv.lib

../build/libcrt.lib: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../build/libdrv.lib: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../build/libsrv.lib: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.c
	@printf "%b" "\033[0;32mCompiling source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../build/libcrt.lib
	@rm -f ../build/libdrv.lib
	@rm -f ../build/libsrv.lib
	@rm -f $(OBJECTS_ALL)