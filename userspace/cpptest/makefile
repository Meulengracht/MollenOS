# Makefile for building a generic userspace application
INCLUDES = -I../include/cxx -I../include
SOURCES = $(wildcard ./*.cpp)
OBJECTS = $(SOURCES:.cpp=.o)

CXXFLAGS = $(GUCXXFLAGS) $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap /entry:__CrtConsoleEntry $(GUCXXLIBRARIES)

.PHONY: all
all: ../bin/cpptest.app

../bin/cpptest.app: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.cpp
	$(CC) -c $(CXXFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJECTS)
	rm -f ../bin/cpptest.app