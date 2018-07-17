# Makefile for building a generic userspace application or library

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../include/cxx -I../include
SOURCES = $(wildcard ./*.c)
OBJECTS = $(SOURCES:.c=.o)

CONFIG_FLAGS = 
CFLAGS = $(GUCFLAGS) $(CONFIG_FLAGS) $(INCLUDES)
LFLAGS = $(GLFLAGS) /version:1.6 /def:libpng.def /lldmap /entry:__CrtLibraryEntry /dll $(GUCLIBRARIES) ../lib/zlib.lib

.PHONY: all
all: ../bin/libpng.dll

../bin/libpng.dll: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	@mv -f ../bin/libpng.lib ../lib/libpng.lib
	@cp pnglibconf.h ../include/pnglibconf.h
	@cp pngconf.h ../include/pngconf.h
	@cp png.h ../include/png.h
	
%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f $(OBJECTS)
	@rm -f ../lib/libpng.lib
	@rm -f ../bin/libpng.dll