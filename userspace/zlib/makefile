# Makefile for building a generic userspace application or library
INCLUDES = -I../include/cxx -I../include
SOURCES = $(wildcard ./*.c)
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GCFLAGS) $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap /entry:__CrtLibraryEntry $(GLIBRARIES)

.PHONY: all
all: ../lib/zlib.dll

../lib/zlib.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJECTS)
	rm -f ../lib/zlib.lib
	rm -f ../lib/zlib.dll