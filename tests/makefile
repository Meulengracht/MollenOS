# Makefile for building all tests appications
#

.PHONY: all
all: bin lib include build_cpptest

bin:
	@mkdir -p $@

lib:
	@mkdir -p $@

include:
	@mkdir -p $@

.PHONY: build_cpptest
build_cpptest:
	@printf "%b" "\033[1;35mChecking if cpptest needs to be built\033[m\n"
	@$(MAKE) -s -C cpptest -f makefile

.PHONY: clean
clean:
	@$(MAKE) -s -C cpptest -f makefile clean
	@rm -rf bin
	@rm -rf include
	@rm -rf lib