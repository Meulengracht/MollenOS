# Makefile for building the standard c++ runtime-libraries for userspace
# This will produce the file libcxx.lib
INCLUDES = -I../include -I../include/cxx -I./include

SOURCES = $(wildcard ./*.cpp)
OBJECTS = $(SOURCES:.cpp=.o)

CXXFLAGS = $(GCXXFLAGS) $(INCLUDES)
LFLAGS = $(GLFLAGS) /noentry /dll /export-all-symbols ../build/libc.lib ../build/libcrt.lib ../deploy/libcxxabi.lib

# default-target
.PHONY: all
all: ../deploy/libcxx.dll

../deploy/libcxx.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../deploy/libcxx.dll
	@rm -f ../deploy/libcxx.lib
	@rm -f $(OBJECTS)