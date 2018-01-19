# Makefile for building a generic userspace application or library
INCLUDES = -I../include/cxx -I../include
SOURCES = $(wildcard ./*.c)
OBJECTS = $(SOURCES:.c=.o)

CONFIG_FLAGS = -DZLIB_DLL
CFLAGS = $(GUCFLAGS) $(CONFIG_FLAGS) $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap /entry:__CrtLibraryEntry $(GUCLIBRARIES)

.PHONY: all
all: ../bin/zlib.dll

../bin/zlib.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	mv -f ../bin/zlib.lib ../lib/zlib.lib
	
%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJECTS)
	rm -f ../lib/zlib.lib
	rm -f ../bin/zlib.dll