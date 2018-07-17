# Makefile for building a generic userspace application or library

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../include/cxx -I../include
SOURCES = $(wildcard ./*.c)
OBJECTS = $(SOURCES:.c=.o)

CONFIG_FLAGS = -DZLIB_DLL
CFLAGS = $(GUCFLAGS) $(CONFIG_FLAGS) $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap /entry:__CrtLibraryEntry /dll $(GUCLIBRARIES)

.PHONY: all
all: ../bin/zlib.dll

../bin/zlib.dll: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	@mv -f ../bin/zlib.lib ../lib/zlib.lib
	@cp zconf.h ../include/zconf.h
	@cp zlib.h ../include/zlib.h
	
%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f $(OBJECTS)
	@rm -f ../lib/zlib.lib
	@rm -f ../bin/zlib.dll