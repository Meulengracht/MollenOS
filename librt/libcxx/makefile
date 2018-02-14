# Makefile for building the standard c++abi runtime-libraries for userspace
# This will produce the file libcxxabi.lib
INCLUDES = -Icxx/include -I../include -Iabi/include/

INT_SOURCES = $(wildcard init/*.c)
ABI_SOURCES = $(wildcard abi/src/*.cpp) $(wildcard init/*.cpp)
CXX_SOURCES = $(wildcard cxx/src/*.cpp)
SHR_SOURCES = main.cpp
INT_OBJECTS = $(INT_SOURCES:.c=.o)
ABI_OBJECTS = $(ABI_SOURCES:.cpp=.o)
CXX_OBJECTS = $(CXX_SOURCES:.cpp=.o)
SHR_OBJECTS = $(SHR_SOURCES:.cpp=.o)

# Configuration flags for each project
SHARED_CONFIG = $(GCXXFLAGS) -fstrict-aliasing -D_LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS -D_LIBCPP_ENABLE_CXX17_REMOVED_UNEXPECTED_FUNCTIONS -D_LIBCPP_BUILDING_LIBRARY
ABI_CONFIG = -funwind-tables -DLIBCXXABI_USE_LLVM_UNWINDER -D_LIBCXXABI_BUILDING_LIBRARY -DHAVE___CXA_THREAD_ATEXIT_IMPL -D_LIBCXXABI_ENABLE_EXCEPTIONS
CXX_CONFIG = -DLIBCXX_BUILDING_LIBCXXABI -DLIBCXXRT -D_LIBCPP_DISABLE_NEW_DELETE_DEFINITIONS
CFLAGS = $(GCFLAGS) $(INCLUDES)
CXXFLAGS = $(SHARED_CONFIG) $(INCLUDES)

STT_LFLAGS = /lib
SHR_LFLAGS = $(GLFLAGS) /entry:__CrtLibraryEntry /dll /lldmap ../deploy/libcxxabi.lib ../build/libc.lib ../build/libcrt.lib ../deploy/libunwind.lib

.PHONY: all
all: dep-unwind all-static

# Libunwind
.PHONY: dep-unwind
dep-unwind:
	@$(MAKE) -s -C unwind -f makefile

# default-static-target
.PHONY: all-static
all-static: ../deploy/libcxx.lib

# default-shared-target
.PHONY: all-shared
all-shared: ../deploy/libcxx.dll

../deploy/libcxx.lib: $(INT_OBJECTS) $(ABI_OBJECTS) $(CXX_OBJECTS)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(STT_LFLAGS) $(INT_OBJECTS) $(ABI_OBJECTS) $(CXX_OBJECTS) /out:$@

../deploy/libcxx.dll: $(ABI_OBJECTS) $(CXX_OBJECTS) $(SHR_OBJECTS)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	$(LD) $(SHR_LFLAGS) $(ABI_OBJECTS) $(CXX_OBJECTS) $(SHR_OBJECTS) /out:$@

$(ABI_OBJECTS): %.o : %.cpp
	@printf "%b" "\033[0;32mCompiling C++ source object " $< "\033[m\n"
	@$(CXX) -c $(CXXFLAGS) $(ABI_CONFIG) -o $@ $<

$(CXX_OBJECTS): %.o : %.cpp
	@printf "%b" "\033[0;32mCompiling C++ source object " $< "\033[m\n"
	@$(CXX) -c $(CXXFLAGS) $(CXX_CONFIG) -o $@ $<

$(SHR_OBJECTS): %.o : %.cpp
	@printf "%b" "\033[0;32mCompiling C++ source object " $< "\033[m\n"
	@$(CXX) -c $(CXXFLAGS) $(CXX_CONFIG) -o $@ $<

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@$(MAKE) -s -C unwind -f makefile clean
	@rm -f ../deploy/libcxx.dll
	@rm -f ../deploy/libcxx.lib
	@rm -f $(INT_OBJECTS)
	@rm -f $(ABI_OBJECTS)
	@rm -f $(CXX_OBJECTS)
	@rm -f $(SHR_OBJECTS)