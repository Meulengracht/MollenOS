# Bootloader makefile
.PHONY: all
all: build build/stage1.sys build/stage2.sys

build:
	@mkdir -p $@
	
build/stage1.sys: stage1/mfs/stage1.asm
	@printf "%b" "\033[0;32mAssembling source object " $< "\033[m\n"
	@$(AS) $(ASFLAGS) stage1/mfs/stage1.asm -o build/stage1.sys

build/stage2.sys: stage2/stage2.asm
	@printf "%b" "\033[0;32mAssembling source object " $< "\033[m\n"
	@cd stage2 && $(AS) $(ASFLAGS) stage2.asm -o stage2.sys && cd ..
	@cp stage2/stage2.sys build/stage2.sys
	@rm stage2/stage2.sys

.PHONY: clean
clean:
	@rm -rf build