# Makefile for building the standard c++abi runtime-libraries for userspace
# This will produce the file libcxxabi.lib
INCLUDES = -I../include -I../include/cxx -I./

SOURCES = $(wildcard ./*.cpp)
OBJECTS = $(SOURCES:.cpp=.o)

# dll - _LIBCXXABI_BUILDING_LIBRARY
CXXFLAGS = $(GCXXFLAGS) -D_DLL -D_LIBCXXABI_BUILDING_LIBRARY -D_NEWLIB_VERSION -fno-ms-compatibility $(INCLUDES)
LFLAGS = /nodefaultlib /subsystem:native /noentry /dll ../build/libc.lib ../build/libcrt.lib

# default-target
.PHONY: all
all: ../build/libcxxabi.dll

../build/libcxxabi.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../build/libcxxabi.dll
	@rm -f ../build/libcxxabi.lib
	@rm -f $(OBJECTS)