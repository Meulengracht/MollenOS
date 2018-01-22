# Makefile for building the standard c++abi runtime-libraries for userspace
# This will produce the file libunwind.lib
INCLUDES =  -I../libcxx/cxx/include -I../include -I./include

# to generate $(wildcard ./*.S) $(SOURCES:.S=.o)
SOURCES_S = $(wildcard ./*.S)
SOURCES_C = $(wildcard ./*.c)
SOURCES_X = libunwind.cpp Unwind-EHABI.cpp
OBJECTS = $(SOURCES_S:.S=.o) $(SOURCES_C:.c=.o) $(SOURCES_X:.cpp=.o)

CONFIG_FLAGS = -DNDEBUG -D_LIBUNWIND_IS_NATIVE_ONLY -D_LIBUNWIND_IS_BAREMETAL
CFLAGS = $(GCFLAGS) -std=c11 -D__OSLIB_UNWIND_IMPLEMENTATION $(CONFIG_FLAGS) $(INCLUDES)
CXXFLAGS = $(GCXXFLAGS) -D__OSLIB_UNWIND_IMPLEMENTATION -fno-rtti -fno-exceptions $(CONFIG_FLAGS) $(INCLUDES)
LFLAGS = $(GLFLAGS) /entry:__CrtLibraryEntry /dll /lldmap ../build/libc.lib ../build/libcrt.lib

# default-target
.PHONY: all
all: ../deploy/libunwind.dll

../deploy/libunwind.dll: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.cpp
	@printf "%b" "\033[0;32mCompiling C++ source object " $< "\033[m\n"
	@$(CXX) -c $(CXXFLAGS) -o $@ $<
%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<
%.o : %.S
	@printf "%b" "\033[0;32mAssembling source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../deploy/libunwind.dll
	@rm -f ../deploy/libunwind.lib
	@rm -f $(OBJECTS)