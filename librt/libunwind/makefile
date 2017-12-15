# Makefile for building the standard c++abi runtime-libraries for userspace
# This will produce the file libunwind.lib
INCLUDES = -I./include -I./ -I../include -I../include/cxx 

# to generate $(wildcard ./*.S) $(SOURCES:.S=.o)
SOURCES_S = $(wildcard ./*.S)
SOURCES_C = $(wildcard ./*.c)
SOURCES_X = libunwind.cpp Unwind-EHABI.cpp
OBJECTS = $(SOURCES_S:.S=.o) $(SOURCES_C:.c=.o) $(SOURCES_X:.cpp=.o)

CFLAGS = $(GCFLAGS) -std=c11 -D_DLL -DCRTDLL -D__OSLIB_UNWIND_IMPLEMENTATION $(INCLUDES)
CXXFLAGS = $(GCXXFLAGS) -D_DLL -DCRTDLL -D__OSLIB_UNWIND_IMPLEMENTATION -fno-rtti $(INCLUDES)
LFLAGS = $(GLFLAGS) /entry:__CrtLibraryEntry /dll /export-all-symbols ../build/libc.lib ../build/libcrt.lib

# default-target
.PHONY: all
all: ../deploy/libunwind.dll

../deploy/libunwind.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<
%.o : %.c
	$(CC) -c $(CFLAGS) -o $@ $<
%.o : %.S
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../deploy/libunwind.dll
	@rm -f ../deploy/libunwind.lib
	@rm -f $(OBJECTS)