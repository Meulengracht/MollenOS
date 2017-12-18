# Makefile for building the standard c++ runtime-libraries for userspace
# This will produce the file libcxx.lib
INCLUDES = -I../include/cxx -I../include -I../libcxxabi/ -I./include

SOURCES = $(wildcard ./*.cpp)
OBJECTS = $(SOURCES:.cpp=.o)

#CXXFLAGS = $(GCXXFLAGS) -D_DLL -DCRTDLL -DLIBCXXRT -D_LIBCPP_BUILDING_LIBRARY $(INCLUDES)
#LFLAGS = $(GLFLAGS) /entry:__CrtLibraryEntry /dll ../build/libc.lib ../build/libcrt.lib ../build/libclang.lib ../deploy/libunwind.lib ../deploy/libcxxabi.lib
CXXFLAGS = $(GCXXFLAGS) -DLIBCXX_BUILDING_LIBCXXABI -DLIBCXXRT -D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS -D_LIBCPP_BUILDING_LIBRARY -D_LIBCPP_DEBUG_LEVEL=2 $(INCLUDES)
LFLAGS = /lib

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