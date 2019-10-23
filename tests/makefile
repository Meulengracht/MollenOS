# Makefile for building all tests appications
#
export GUCLIBRARIES = crt.lib compiler-rt.lib ddk.lib c.lib m.lib
export GUCXXLIBRARIES = static_c++.lib static_c++abi.lib unwind.lib crt.lib compiler-rt.lib ddk.lib c.lib m.lib

.PHONY: all
all: bin lib include build_cpptest build_scpptest build_wm_server build_wm_client

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

.PHONY: build_scpptest
build_scpptest:
	@printf "%b" "\033[1;35mChecking if scpptest needs to be built\033[m\n"
	@$(MAKE) -s -C scpptest -f makefile

.PHONY: build_wm_server
build_wm_server:
	@printf "%b" "\033[1;35mChecking if wm_server_test needs to be built\033[m\n"
	@$(MAKE) -s -C wm_server_test -f makefile

.PHONY: build_wm_client
build_wm_client:
	@printf "%b" "\033[1;35mChecking if wm_client_test needs to be built\033[m\n"
	@$(MAKE) -s -C wm_client_test -f makefile

.PHONY: clean
clean:
	@$(MAKE) -s -C cpptest -f makefile clean
	@$(MAKE) -s -C scpptest -f makefile clean
	@$(MAKE) -s -C wm_server_test -f makefile clean
	@$(MAKE) -s -C wm_client_test -f makefile clean
	@rm -rf bin
	@rm -rf include
	@rm -rf lib