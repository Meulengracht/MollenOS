CXX ?= g++

SOURCE = $(wildcard *.cpp) \
		 $(wildcard common/*.cpp) \
		 $(wildcard generators/*.cpp) \
		 $(wildcard generators/c/*.cpp) \
		 $(wildcard parser/*.cpp)
OBJECTS = $(SOURCE:.cpp=.o)
DEP = $(OBJECTS:.o=.d)

gracht: $(OBJECTS)
	$(CXX) -o $@ $^

-include $(DEP)   # include all dep files in the makefile

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.cpp
	@$(CXX) $(CFLAGS) $< -MM -MT $(@:.d=.o) >$@

.PHONY: clean
clean:
	rm -f $(OBJECTS) gracht
	rm -f $(DEP)