# Makefile for building a generic userspace application
ASFLAGS = -f win32 -Xvc
INCLUDES = -I../include/cxx -I../include
SOURCES = $(wildcard ./*.cpp)
ASM = graphics/displays/display_framebuffer_$(VALI_ARCH).s
OBJECTS = $(SOURCES:.cpp=.o) $(ASM:.s=.o)

CXXFLAGS = $(GUCXXFLAGS) $(INCLUDES)
LFLAGS = $(GLFLAGS) /lldmap /entry:__CrtConsoleEntry $(GUCXXLIBRARIES) ../lib/gallium-osmesa.lib

.PHONY: all
all: ../bin/vioarr.app

../bin/vioarr.app: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating application " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.cpp
	@printf "%b" "\033[0;32mCompiling C++ source object " $< "\033[m\n"
	@$(CC) -c $(CXXFLAGS) -o $@ $<

%.o : %.s
	@printf "%b" "\033[0;32mAssembling source object " $< "\033[m\n"
	@$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	@rm -f $(OBJECTS)
	@rm -f ../bin/vioarr.app