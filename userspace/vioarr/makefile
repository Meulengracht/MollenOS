# Makefile for building a generic userspace application
INCLUDES = -I../include/cxx -I../include
SOURCES = $(wildcard ./*.cpp)
OBJECTS = $(SOURCES:.cpp=.o)

CXXFLAGS = $(GUCXXFLAGS) $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap /entry:__CrtConsoleEntry $(GUCXXLIBRARIES)

.PHONY: all
all: ../bin/vioarr.app

../bin/vioarr.app: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating application " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.cpp
	@printf "%b" "\033[0;32mCompiling C++ source object " $< "\033[m\n"
	@$(CC) -c $(CXXFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f $(OBJECTS)
	@rm -f ../bin/vioarr.app