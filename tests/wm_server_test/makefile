# Makefile for building a generic userspace application

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../../protocols \
		   -I../../librt/libwm/include \
		   -I../../librt/libm/include \
		   -I../../librt/libc/include \
		   -I../../librt/include
LIBRARIES = libwm.lib

CFLAGS = $(GUCFLAGS) $(INCLUDES)
CXXFLAGS = $(GUCXXFLAGS) -static -I../../librt/libcxx/libcxx/include $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap -LIBPATH:../../librt/build -LIBPATH:../../librt/deploy $(LIBRARIES)

.PHONY: all
all: test_utils_protocol_server.h ../bin/wmsrv.app

test_utils_protocol_server.h: ../../protocols/test_protocol.xml
	@printf "%b" "\033[0;36mRegenerating protocol " $@ "\033[m\n"
	python ../../protocols/gracht_generator.py --protocol $< --lang-c --server
	
../bin/wmsrv.app: main.o
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
	@rm -f test_utils_protocol*
	@rm -f ../bin/wmsrv.app
