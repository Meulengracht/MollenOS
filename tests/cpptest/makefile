# Makefile for building a generic userspace application

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../../librt/libcxx/libcxx/include -I../../librt/libm/include -I../../librt/libc/include -I../../librt/libc/include/$(VALI_ARCH) -I../../librt/libddk/include -I../../librt/include

CFLAGS = $(GUCFLAGS) $(INCLUDES)
CXXFLAGS = $(GUCXXFLAGS) -static $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap -LIBPATH:../../librt/build -LIBPATH:../../librt/deploy

.PHONY: all
all: ../bin/cpplibtest.dll ../bin/cpptest.app

../bin/cpplibtest.dll: lib.o
	@printf "%b" "\033[0;36mCreating shared library " $@ "\033[m\n"
	@$(LD) /dll /entry:__CrtLibraryEntry $(LFLAGS) $(GUCXXLIBRARIES) lib.o /out:$@

../bin/cpptest.app: main.o test-double.o
	@printf "%b" "\033[0;36mCreating application " $@ "\033[m\n"
	@$(LD) /entry:__CrtConsoleEntry ../bin/cpplibtest.lib $(LFLAGS) $(GUCXXLIBRARIES) main.o test-double.o /out:$@
	
%.o : %.cpp
	@printf "%b" "\033[0;32mCompiling C++ source object " $< "\033[m\n"
	$(CXX) -c $(CXXFLAGS) -o $@ $<

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f main.o lib.o test-double.o test-float.o
	@rm -f ../bin/cpptest.app
	@rm -f ../bin/cpplibtest.dll
	@rm -f ../bin/cpplibtest.lib