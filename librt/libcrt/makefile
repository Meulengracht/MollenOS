# Makefile for building the standard c runtime-libraries for userspace
# This will produce the file libcrt.lib, libsrv.lib, libdrv.lib
ASFLAGS = -f win32 -Xvc

INCLUDES = -I../include

ASM_SRCS = $(wildcard vscrt/*.s)
C_SRCS_ALL = $(wildcard vscrt/*.c) \
			 $(wildcard ./*.c)
OBJECTS_ALL = $(ASM_SRCS:.s=.o) $(C_SRCS_ALL:.c=.o)

C_SRCS =  $(wildcard vscrt/*.c)

ifeq ($(MAKECMDGOALS),libcrt)
	SOURCES = $(wildcard vscrt/*.c) entry.c
	CFLAGS = $(GCFLAGS) $(INCLUDES)
else 
	ifeq ($(MAKECMDGOALS),libdrv)
		SOURCES = $(wildcard vscrt/*.c) driver.c
		CFLAGS = $(GCFLAGS) -D__DRIVER_IMPL $(INCLUDES)
	else
		SOURCES = $(wildcard vscrt/*.c) service.c
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
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../build/libdrv.lib: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

../build/libsrv.lib: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

%.o : %.s
	$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	@rm -f ../build/libcrt.lib
	@rm -f ../build/libdrv.lib
	@rm -f ../build/libsrv.lib
	@rm -f $(OBJECTS_ALL)