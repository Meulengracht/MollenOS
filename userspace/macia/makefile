# Makefile for building a generic userspace application
INCLUDES = -I../include/cxx -I../include
SOURCES = $(wildcard generator/*.cpp) \
		  $(wildcard interpreter/*.cpp) \
		  $(wildcard lexer/*.cpp) \
		  $(wildcard parser/*.cpp) \
		  $(wildcard shared/*.cpp) \
		  Macia.cpp
OBJECTS = $(SOURCES:.cpp=.o)

CXXFLAGS = $(GUCXXFLAGS) $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap /entry:__CrtConsoleEntry $(GUCXXLIBRARIES)

.PHONY: all
all: ../bin/macia.app

../bin/macia.app: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.cpp
	$(CC) -c $(CXXFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJECTS)
	rm -f ../bin/macia.app