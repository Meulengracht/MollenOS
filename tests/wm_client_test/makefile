# Makefile for building a generic userspace application

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../../protocols \
		   -I../../librt/libwm/include \
		   -I../../librt/libm/include \
		   -I../../librt/libc/include \
		   -I../../librt/include

SOURCES = main.c ../../protocols/test/test_protocol_client.c
OBJECTS = $(SOURCES:.c=.o)

CFLAGS = $(GUCFLAGS) $(INCLUDES)
CXXFLAGS = $(GUCXXFLAGS) -static -I../../librt/libcxx/libcxx/include $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap -LIBPATH:../../librt/build -LIBPATH:../../librt/deploy libwm.lib

.PHONY: all
all: ../bin/wmclient.app

../bin/wmclient.app: $(OBJECTS)
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
	@rm -f ../bin/wmclient.app
