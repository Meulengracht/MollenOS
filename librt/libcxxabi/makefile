# Makefile for building the standard c++abi runtime-libraries for userspace
# This will produce the file libcxxabi.lib
INCLUDES = -I../include -I../include/cxx -I./

SOURCES = $(wildcard ./*.cpp)
OBJECTS = $(SOURCES:.cpp=.o)

# -DHAVE___CXA_THREAD_ATEXIT_IMPL
CXXFLAGS = $(GCXXFLAGS) -std=c++11 -funwind-tables -D_DLL -DCRTDLL -DLIBCXXABI_USE_LLVM_UNWINDER -D_LIBCXXABI_ENABLE_EXCEPTIONS -DHAVE___CXA_THREAD_ATEXIT_IMPL -D_LIBCXXABI_BUILDING_LIBRARY -D_NEWLIB_VERSION $(INCLUDES)
LFLAGS = $(GLFLAGS) /noentry /dll /export-all-symbols ../build/libc.lib ../build/libcrt.lib ../deploy/libunwind.lib

# default-target
.PHONY: all
all: ../deploy/libcxxabi.dll

../deploy/libcxxabi.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../deploy/libcxxabi.dll
	@rm -f ../deploy/libcxxabi.lib
	@rm -f $(OBJECTS)