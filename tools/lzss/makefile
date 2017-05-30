# Script for building the lzss utility for compression
# Used for compression images needed

.PHONY: all
all: ../../lzss

../../lzss: 
	clang main.c aplib64.a -o $@

.PHONY: clean
clean:
	rm -f ../../lzss