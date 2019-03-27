# Makefile for building a generic userspace application

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../../librt/libcxx/cxx/include -I../../librt/libc/include -I../../librt/libc/include/$(VALI_ARCH) -I../../librt/libddk/include -I../../librt/include

CFLAGS = $(GUCFLAGS) $(INCLUDES)
CXXFLAGS = $(GUCXXFLAGS) $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap -LIBPATH:../../librt/build -LIBPATH:../../librt/deploy

.PHONY: all
all: ../bin/stest.app

../bin/stest.app: main.o
	@printf "%b" "\033[0;36mCreating application " $@ "\033[m\n"
	@$(LD) /entry:__CrtConsoleEntry $(LFLAGS) $(GUCXXLIBRARIES) main.o /out:$@
	
%.o : %.cpp
	@printf "%b" "\033[0;32mCompiling C++ source object " $< "\033[m\n"
	@$(CXX) -c $(CXXFLAGS) -o $@ $<

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f main.o
	@rm -f ../bin/stest.app