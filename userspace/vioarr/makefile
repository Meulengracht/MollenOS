# Makefile for building a generic userspace application

# Include all the definitions for os
include ../../config/common.mk

ASFLAGS = -f win32 -Xvc
INCLUDES = -I../include/cxx -I../include -I. -Igraphics
NVG_SOURCES = engine/backend/nanovg.c
SHARED_SOURCES = $(wildcard engine/elements/*.cpp) \
				 $(wildcard engine/dialogs/*.cpp) \
				 $(wildcard engine/*.cpp) \
				 $(wildcard scenes/*.cpp) \
				 $(wildcard utils/*.cpp) \
				 $(wildcard *.cpp)

OSMESA_ASM = graphics/opengl/osmesa/display_osmesa_$(VALI_ARCH).s
OSMESA_OBJECTS = $(OSMESA_ASM:.s=.o) $(NVG_SOURCES:.c=.o) $(SHARED_SOURCES:.cpp=.o)

SOFT_SOURCES = 
SOFT_ASM = graphics/displays/fb/display_framebuffer_$(VALI_ARCH).s
SOFT_OBJECTS = $(SHARED_SOURCES:.cpp=.o) $(SOFT_SOURCES:.cpp=.o) $(SOFT_ASM:.s=.o)

CFLAGS = $(GUCFLAGS) -DGL_VIOARR_LOADER -DNANOVG_GL3_IMPLEMENTATION -DFONS_USE_FREETYPE $(INCLUDES)
CXXFLAGS = $(GUCXXFLAGS) $(INCLUDES)
ifeq ($(MAKECMDGOALS),osmesa)
	OBJECTS = $(OSMESA_OBJECTS)
	CXXFLAGS += -D_VIOARR_OSMESA -D_VIOARR_OPENGL -DGL_GLEXT_PROTOTYPES -DGL_VIOARR_LOADER -DNANOVG_GL3_IMPLEMENTATION
	LFLAGS = $(GLFLAGS) /lldmap /entry:__CrtConsoleEntry $(GUCXXLIBRARIES) ../lib/gallium-osmesa.lib ../lib/libfreetype.lib
else
	OBJECTS = $(SOFT_OBJECTS)
	LFLAGS = $(GLFLAGS) /lldmap /entry:__CrtConsoleEntry $(GUCXXLIBRARIES) ../lib/libpng.lib
endif

.PHONY: all
all: ../bin/vioarr.app

.PHONY: osmesa
osmesa: all

../bin/vioarr.app: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating application " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@
	
%.o : %.cpp
	@printf "%b" "\033[0;32mCompiling C++ source object " $< "\033[m\n"
	@$(CC) -c $(CXXFLAGS) -o $@ $<

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C++ source object " $< "\033[m\n"
	@$(CC) -c $(CFLAGS) -o $@ $<

%.o : %.s
	@printf "%b" "\033[0;32mAssembling source object " $< "\033[m\n"
	@$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	@rm -f $(OBJECTS)
	@rm -f ../bin/vioarr.app