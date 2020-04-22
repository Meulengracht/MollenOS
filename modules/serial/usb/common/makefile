# Makefile for building a module dll that can be loaded by MollenOS
# Valid for drivers

# Include all the definitions for os
include ../../../../config/common.mk

.PHONY: all
all: ctt_driver_protocol_server.c ctt_usbhost_protocol_server.c

ctt_driver_protocol_server.c: ../../../../protocols/contract_protocols.xml
	@printf "%b" "\033[0;36mRegenerating protocol " $@ "\033[m\n"
	python ../../../../protocols/gracht_generator.py --protocol $< --lang-c --server --include driver

ctt_usbhost_protocol_server.c: ../../../../protocols/contract_protocols.xml
	@printf "%b" "\033[0;36mRegenerasting protocol " $@ "\033[m\n"
	python ../../../../protocols/gracht_generator.py --protocol $< --lang-c --server --include usbhost

.PHONY: clean
clean:
	@rm -f ctt_*