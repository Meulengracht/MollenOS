# Bootloader makefile
.PHONY: all
all: build build/stage1.sys build/stage2.sys

build:
	mkdir $@
	
build/stage1.sys:
	$(ASM) stage1/mfs/stage1.asm -o build/stage1.sys

build/stage2.sys:
	cd stage2 && $(ASM) stage2.asm -o stage2.sys && cd ..
	$(FCOPY) stage2\stage2.sys build\stage2.sys
	del stage2\stage2.sys

.PHONY: clean
clean:
	rmdir /s /q build