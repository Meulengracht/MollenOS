# Script for building the lzss utility for compression
# Used for compression images needed

.PHONY: all
all: ../../lzss

../../lzss: main.c
	@printf "%b" "\033[0;36mCreating tool " $@ "\033[m\n"
	@gcc main.c aplib64.a -o $@

.PHONY: clean
clean:
	@rm -f ../../lzss
