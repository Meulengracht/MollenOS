# Script for building the file2c utility for the os

.PHONY: all
all: ../../file2c

../../file2c: main.c
	@printf "%b" "\033[0;36mCreating tool " $@ "\033[m\n"
	@gcc main.c -o $@

.PHONY: clean
clean:
	@rm -f ../../file2c
