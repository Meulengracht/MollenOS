# Makefile for building a generic userspace application or library

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../include/cxx -I../include -Iinclude
SOURCES = src/autofit/autofit.c \
		  src/base/ftbase.c \
		  src/base/ftbbox.c \
		  src/base/ftbdf.c \
		  src/base/ftbitmap.c \
		  src/base/ftcid.c \
		  src/base/ftfntfmt.c \
		  src/base/ftfstype.c \
		  src/base/ftgasp.c \
		  src/base/ftglyph.c \
		  src/base/ftgxval.c \
		  src/base/ftinit.c \
		  src/base/ftlcdfil.c \
		  src/base/ftmm.c \
		  src/base/ftotval.c \
		  src/base/ftpatent.c \
		  src/base/ftpfr.c \
		  src/base/ftstroke.c \
		  src/base/ftsynth.c \
		  src/base/ftsystem.c \
		  src/base/fttype1.c \
		  src/base/ftwinfnt.c \
		  src/base/ftdebug.c \
		  src/bdf/bdf.c \
		  src/bzip2/ftbzip2.c \
		  src/cache/ftcache.c \
		  src/cff/cff.c \
		  src/cid/type1cid.c \
		  src/gzip/ftgzip.c \
		  src/lzw/ftlzw.c \
		  src/pcf/pcf.c \
		  src/pfr/pfr.c \
		  src/psaux/psaux.c \
		  src/pshinter/pshinter.c \
		  src/psnames/psnames.c \
		  src/raster/raster.c \
		  src/sfnt/sfnt.c \
		  src/smooth/smooth.c \
		  src/truetype/truetype.c \
		  src/type1/type1.c \
		  src/type42/type42.c \
		  src/winfonts/winfnt.c \
		  main.c
OBJECTS = $(SOURCES:.c=.o)

CONFIG_FLAGS = -DFT2_BUILD_LIBRARY -DFT_SHARED_LIBRARY
CFLAGS = $(GUCFLAGS) $(CONFIG_FLAGS) $(INCLUDES)
LFLAGS = $(GLFLAGS) /version:2.9 /lldmap /entry:__CrtLibraryEntry /dll $(GUCLIBRARIES) ../lib/zlib.lib ../lib/libpng.lib

.PHONY: all
all: ../bin/libfreetype.dll

../bin/libfreetype.dll: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	@mv -f ../bin/libfreetype.lib ../lib/libfreetype.lib
	@cp -a include/* ../include/
	@rm -rf ../include/freetype/internal
	
%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f $(OBJECTS)
	@rm -f ../lib/libfreetype.lib
	@rm -f ../bin/libfreetype.dll