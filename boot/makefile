# Bootloader makefile
.PHONY: all
all: build build/stage1.sys build/stage2.sys build/ap.sys

# Include all the definitions for os
include ../config/common.mk

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

build/ap.sys: stage2/ap.asm
	@printf "%b" "\033[0;32mAssembling source object " $< "\033[m\n"
	@cd stage2 && $(AS) $(ASFLAGS) -DTRAMPOLINE_LOCATION=0x8000 -D__BOOT_SILENCE ap.asm -o ap.sys && cd ..
	@cp stage2/ap.sys build/ap.sys
	@rm stage2/ap.sys
	../file2c build/ap.sys ../kernel/arch/x86/components/smp_trampoline.c __GlbTramplineCode

.PHONY: clean
clean:
	@rm -rf build