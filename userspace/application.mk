# Makefile for building a generic userspace application that follows the
# application build and assembly process specified by mollenos

# Sanitize for name
ifndef APP_NAME
$(error APP_NAME is not set)
endif

APP_INCLUDES = -I$(include_path)/cxx -I$(include_path) $(INCLUDES)
APP_CFLAGS = $(GUCFLAGS) $(CFLAGS) $(APP_INCLUDES)
APP_CXXFLAGS = $(GUCXXFLAGS) $(CXXFLAGS) $(APP_INCLUDES)
APP_LFLAGS = $(GLFLAGS) /lldmap /entry:__CrtConsoleEntry $(GUCXXLIBRARIES) $(LFLAGS)

APP_OBJECTS = $(APP_SOURCES_CXX:.cpp=.o) $(APP_SOURCES_C:.cpp=.o)

../bin/$(APP_NAME).app: $(APP_OBJECTS)
	@printf "%b" "\033[0;36mCreating application " $@ "\033[m\n"
	@$(LD) $(APP_LFLAGS) $(APP_OBJECTS) /out:$@
	
%.o : %.cpp
	@printf "%b" "\033[0;32mCompiling C++ source object " $< "\033[m\n"
	@$(CC) -c $(APP_CXXFLAGS) -o $@ $<

%.o : %.c
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CC) -c $(APP_CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f $(APP_OBJECTS)
	@rm -f ../bin/$(APP_NAME).app