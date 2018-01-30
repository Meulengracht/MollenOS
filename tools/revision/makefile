# Script for building the revision utility for versioning the os

.PHONY: all
all: ../../revision

../../revision: main.c
	@printf "%b" "\033[0;36mCreating tool " $@ "\033[m\n"
	@clang main.c -o $@

.PHONY: clean
clean:
	@rm -f ../../revision