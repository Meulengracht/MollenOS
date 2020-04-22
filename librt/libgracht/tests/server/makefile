# Makefile for building a generic userspace application

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../../protocols \
		   -I../../librt/libgracht/include \
		   -I../../librt/libm/include \
		   -I../../librt/libc/include \
		   -I../../librt/include
LIBRARIES = libgracht.lib

SOURCES = test_utils_protocol_server.c main.c 
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GUCFLAGS) $(INCLUDES)
CXXFLAGS = $(GUCXXFLAGS) -static -I../../librt/libcxx/libcxx/include $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap -LIBPATH:../../librt/build -LIBPATH:../../librt/deploy $(LIBRARIES)

.PHONY: all
all: ../bin/wmsrv.app

test_utils_protocol_server.c: ../../protocols/test_protocol.xml
	@printf "%b" "\033[0;36mRegenerating protocol " $@ "\033[m\n"
	python ../../protocols/gracht_generator.py --protocol $< --lang-c --server
	
../bin/wmsrv.app: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating application " $@ "\033[m\n"
	@$(LD) /entry:__CrtConsoleEntry $(LFLAGS) $(GUCXXLIBRARIES) $(OBJECTS) /out:$@
	
%.o : %.cpp
	@printf "%b" "\033[0;32mCompiling C++ source object " $< "\033[m\n"
	@$(CXX) -c $(CXXFLAGS) -o $@ $<

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f $(OBJECTS)
	@rm -f test_utils_protocol*
	@rm -f ../bin/wmsrv.app
