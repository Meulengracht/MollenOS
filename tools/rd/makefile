# Script for building the lzss utility for compression
# Used for compression images needed

.PHONY: all
all: ../../rd

../../rd: main.c
	@printf "%b" "\033[0;36mCreating tool " $@ "\033[m\n"
	@gcc main.c -o $@

.PHONY: clean
clean:
	@rm -f ../../rd
