# Makefile for building the standard c-libraries for both userspace and kernel
# - libgracht

# Include all the definitions for os
include ../../config/common.mk

INCLUDES = -Iinclude -I../include -I../libddk/include -I../libds/include -I../libc/include
SOURCES = $(wildcard *.c) $(wildcard link/*.c) $(wildcard os/vali/*.c)
OBJECTS = $(SOURCES:.c=.o)

NATIVE_INCLUDES = -Iinclude
NATIVE_SOURCES = $(wildcard *.c) $(wildcard link/*.c)
NATIVE_OBJECTS = $(NATIVE_SOURCES:.c=.o)

TEST_SERVER_SOURCES = tests/test_utils_protocol_server.c $(wildcard tests/server/*.c)
TEST_SERVER_OBJECTS = $(TEST_SERVER_SOURCES:.c=.o)

TEST_CLIENT_SOURCES = tests/test_utils_protocol_client.c $(wildcard tests/client/*.c)
TEST_CLIENT_OBJECTS = $(TEST_CLIENT_SOURCES:.c=.o)

# Setup flags
all: CFLAGS = $(CC) -c $(GCFLAGS) $(INCLUDES)
all: LFLAGS = /lib

native: CFLAGS = gcc -c $(NATIVE_INCLUDES)
native: LFLAGS = ../native/libgracht.a -lrt -lc

# default-target
.PHONY: all
all: ../deploy/libgracht.lib 

../deploy/libgracht.lib: $(OBJECTS)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@$(LD) $(LFLAGS) $(OBJECTS) /out:$@

.PHONY: native
native: ../native/libgracht.a ../native/gracht_server ../native/gracht_client

../native/libgracht.a: $(NATIVE_OBJECTS)
	@mkdir -p $(dir $@)
	@printf "%b" "\033[0;36mCreating static library " $@ "\033[m\n"
	@ar rcs $@ $(NATIVE_OBJECTS)

../native/gracht_server: $(TEST_SERVER_OBJECTS)
	@mkdir -p $(dir $@)
	@printf "%b" "\033[0;36mCreating test server " $@ "\033[m\n"
	@gcc $(TEST_SERVER_OBJECTS) $(LFLAGS) -o $@

../native/gracht_client: $(TEST_CLIENT_OBJECTS)
	@mkdir -p $(dir $@)
	@printf "%b" "\033[0;36mCreating test server " $@ "\033[m\n"
	@gcc $(TEST_CLIENT_OBJECTS) $(LFLAGS) -o $@

tests/test_utils_protocol_client.c: ../../protocols/test_protocol.xml
	@printf "%b" "\033[0;36mRegenerating protocol " $@ "\033[m\n"
	python ../../protocols/gracht_generator.py --protocol $< --out $(dir $@) --lang-c --client

tests/test_utils_protocol_server.c: ../../protocols/test_protocol.xml
	@printf "%b" "\033[0;36mRegenerating protocol " $@ "\033[m\n"
	python ../../protocols/gracht_generator.py --protocol $< --out $(dir $@) --lang-c --server
	
%.o : %.c
	@mkdir -p $(dir $@)
	@printf "%b" "\033[0;32mCompiling C source object " $< "\033[m\n"
	@$(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	@rm -f ../native/libgracht.a
	@rm -f ../native/gracht_server
	@rm -f ../native/gracht_client
	@rm -f ../deploy/libgracht.lib
	@rm -f tests/test_utils_protocol*
	@rm -f $(OBJECTS)