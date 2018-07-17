# Makefile for building a generic userspace application

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -I../include/cxx -I../include
SOURCES = $(wildcard generator/*.cpp) \
		  $(wildcard interpreter/*.cpp) \
		  $(wildcard lexer/*.cpp) \
		  $(wildcard parser/*.cpp) \
		  $(wildcard shared/*.cpp) \
		  macia.cpp
OBJECTS = $(SOURCES:.cpp=.o)

CXXFLAGS = $(GUCXXFLAGS) $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap /entry:__CrtConsoleEntry $(GUCXXLIBRARIES)

.PHONY: all
all: ../bin/macia.app

../bin/macia.app: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating application " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.cpp
	@printf "%b" "\033[0;32mCompiling C++ source object " $< "\033[m\n"
	@$(CXX) -c $(CXXFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f $(OBJECTS)
	@rm -f ../bin/macia.app