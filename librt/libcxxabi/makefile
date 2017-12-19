# Makefile for building the standard c++abi runtime-libraries for userspace
# This will produce the file libcxxabi.lib
INCLUDES = -I../include -I../include/cxx -I./

SOURCES = $(wildcard ./*.cpp)
OBJECTS = $(SOURCES:.cpp=.o)

CONFIG_FLAGS = -fstrict-aliasing -funwind-tables -DLIBCXXABI_USE_LLVM_UNWINDER -D_LIBCXXABI_BUILDING_LIBRARY -D_LIBCPP_DISABLE_EXTERN_TEMPLATE -DHAVE___CXA_THREAD_ATEXIT_IMPL -D_LIBCXXABI_ENABLE_EXCEPTIONS
CXXFLAGS = $(GCXXFLAGS) $(CONFIG_FLAGS) $(INCLUDES)
#-DCRTDLL
#LFLAGS = $(GLFLAGS) /entry:__CrtLibraryEntry /dll /def:libcxxabi.def ../build/libc.lib ../build/libcrt.lib ../deploy/libunwind.lib
LFLAGS = /lib

# default-target
.PHONY: all
all: ../deploy/libcxxabi.lib

../deploy/libcxxabi.lib: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<

.PHONY: clean
clean:
	#@rm -f ../deploy/libcxxabi.dll
	@rm -f ../deploy/libcxxabi.lib
	@rm -f $(OBJECTS)