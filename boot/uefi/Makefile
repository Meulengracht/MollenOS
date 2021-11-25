# SPDX-License-Identifier: BSD-3-Clause

CC=clang
TRIPLE=x86_64-pc-win32-coff
CFLAGS=--target=$(TRIPLE) -Wall -Werror -Wno-format \
	   -mno-red-zone -fno-stack-protector -fshort-wchar \
	   -Iinclude/edk2/MdePkg/Include \
	   -Iinclude/edk2/MdePkg/Include/X64 \
	   -Iinclude
AS=llvm-mc
ASFLAGS=-triple=$(TRIPLE) -filetype=obj
LD=lld-link
LDFLAGS=-subsystem:efi_application -nodefaultlib

SRCS=$(wildcard *.c)
ASMS=$(wildcard *.s)
OBJS=$(SRCS:.c=.o) $(ASMS:.s=.o)
TARGET=main.efi

IMG=image
APP=$(IMG)/$(TARGET)
STARTUP=$(IMG)/startup.nsh

URLBASE=https://github.com/retrage/edk2-nightly/raw/master/bin
OVMFCODE=RELEASEX64_OVMF_CODE.fd
OVMFVARS=RELEASEX64_OVMF_VARS.fd

QEMU=qemu-system-x86_64
QEMUFLAGS=-drive format=raw,file=fat:rw:$(IMG) \
		  -drive if=pflash,format=raw,readonly,file=$(OVMFCODE) \
		  -drive if=pflash,format=raw,file=$(OVMFVARS) \
		  -nodefaults \
		  -nographic \
		  -serial stdio

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -entry:EfiMain $^ -out:$@

$(OVMFCODE):
	wget -q -O $@ $(URLBASE)/$@

$(OVMFVARS):
	wget -q -O $@ $(URLBASE)/$@

$(APP): $(TARGET)
	mkdir -p $(dir $@)
	cp $(TARGET) $@

$(STARTUP): $(APP)
	echo "fs0:" > $@
	echo "$(TARGET)" > $@

run: $(APP) $(STARTUP) $(OVMFCODE) $(OVMFVARS)
	$(QEMU) $(QEMUFLAGS)

clean:
	rm -rf $(TARGET) $(OBJS) $(IMG)

clean-all: clean
	rm -rf $(OVMFCODE) $(OVMFVARS)

.PHONY: all run clean clean-all
