# Makefile for building the mollenos drivers
# Includes drivers that run user-space apis
# - drivers

.PHONY: all
all: build mfs ahci hpet $(arch)

build:
	@mkdir -p $@

# Architecture specific drivers
.PHONY: i386
i386:
	$(MAKE) -C arch/x86/cmos -f makefile
	$(MAKE) -C arch/x86/pit -f makefile
	$(MAKE) -C arch/x86/ps2 -f makefile

# Shared architecture targets below
.PHONY: mfs
mfs:
	$(MAKE) -C filesystems/mfs -f makefile

.PHONY: ahci
ahci:
	$(MAKE) -C storage/ahci -f makefile

.PHONY: hpet
hpet:
	$(MAKE) -C timers/hpet -f makefile

# Cleanup - architecture i386
.PHONY: clean_i386
clean_i386:
	$(MAKE) -C arch/x86/cmos -f makefile clean
	$(MAKE) -C arch/x86/pit -f makefile clean
	$(MAKE) -C arch/x86/ps2 -f makefile clean

# Cleanup - shared
.PHONY: clean
clean: clean_$(arch)
	$(MAKE) -C filesystems/mfs -f makefile clean
	$(MAKE) -C storage/ahci -f makefile clean
	$(MAKE) -C timers/hpet -f makefile clean
	rm -rf build