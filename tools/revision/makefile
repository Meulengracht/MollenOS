# Script for building the revision utility for versioning the os

.PHONY: all
all: ../../revision

../../revision: 
	clang main.c -o $@

.PHONY: clean
clean:
	rm -f ../../revision