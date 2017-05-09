# Bootloader makefile
.PHONY: all
all: build build/stage1.sys build/stage2.sys

build:
	mkdir -p $@
	
build/stage1.sys:
	$(AS) $(ASFLAGS) stage1/mfs/stage1.asm -o build/stage1.sys

build/stage2.sys:
	cd stage2 && $(AS) $(ASFLAGS) stage2.asm -o stage2.sys && cd ..
	$(FCOPY) stage2/stage2.sys build/stage2.sys
	rm stage2/stage2.sys

.PHONY: clean
clean:
	rm -rf build