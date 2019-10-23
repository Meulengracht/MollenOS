# Makefile for building a generic userspace application

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../../librt/libwm/include -I../../librt/libm/include -I../../librt/libc/include -I../../librt/include

CFLAGS = $(GUCFLAGS) $(INCLUDES)
CXXFLAGS = $(GUCXXFLAGS) -static -I../../librt/libcxx/libcxx/include $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap -LIBPATH:../../librt/build -LIBPATH:../../librt/deploy libwm.lib

.PHONY: all
all: ../bin/wmclient.app

../bin/wmclient.app: main.o test_protocol_client.o
	@printf "%b" "\033[0;36mCreating application " $@ "\033[m\n"
	@$(LD) /entry:__CrtConsoleEntry $(LFLAGS) $(GUCXXLIBRARIES) main.o test_protocol_client.o /out:$@
	
%.o : %.cpp
	@printf "%b" "\033[0;32mCompiling C++ source object " $< "\033[m\n"
	@$(CXX) -c $(CXXFLAGS) -o $@ $<

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f *.o
	@rm -f ../bin/wmclient.app
