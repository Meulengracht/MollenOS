# Makefile for building a generic userspace application
INCLUDES = -I../include -I../include/cxx
SOURCES = $(wildcard ./*.cpp)
OBJECTS = $(SOURCES:.cpp=.o)

CXXFLAGS = $(GCXXFLAGS) $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap /entry:__CrtConsoleEntry $(GLIBRARIES)

.PHONY: all
all: ../build/cpptest.app

../build/cpptest.app: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.cpp
	$(CC) -c $(CXXFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f $(OBJECTS)
	rm -f ../build/cpptest.app