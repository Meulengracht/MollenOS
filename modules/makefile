# Makefile for building the mollenos drivers
# Includes drivers that run user-space apis
# - drivers

.PHONY: all
all: build mfs ahci hpet ohci uhci $(arch)

build:
	@mkdir -p $@

# Architecture specific drivers
.PHONY: i386
i386:
	$(MAKE) -C arch/x86/cmos -f makefile
	$(MAKE) -C arch/x86/pit -f makefile
	$(MAKE) -C arch/x86/ps2 -f makefile

.PHONY: amd64
amd64:
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

.PHONY: msd
msd:
	$(MAKE) -C storage/msd -f makefile

.PHONY: hpet
hpet:
	$(MAKE) -C timers/hpet -f makefile

.PHONY: hid
hid:
	$(MAKE) -C input/hid -f makefile

# Usb drivers below here
.PHONY: ohci
ohci:
	$(MAKE) -C serial/usb/ohci -f makefile

.PHONY: uhci
uhci:
	$(MAKE) -C serial/usb/uhci -f makefile

.PHONY: ehci
ehci:
	$(MAKE) -C serial/usb/ehci -f makefile

.PHONY: xhci
xhci:
	$(MAKE) -C serial/usb/xhci -f makefile

# Cleanup - architecture i386
.PHONY: clean_i386
clean_i386:
	$(MAKE) -C arch/x86/cmos -f makefile clean
	$(MAKE) -C arch/x86/pit -f makefile clean
	$(MAKE) -C arch/x86/ps2 -f makefile clean

# Cleanup - architecture amd64
.PHONY: clean_amd64
clean_amd64:
	$(MAKE) -C arch/x86/cmos -f makefile clean
	$(MAKE) -C arch/x86/pit -f makefile clean
	$(MAKE) -C arch/x86/ps2 -f makefile clean

# Cleanup - shared
.PHONY: clean
clean: clean_$(arch)
	$(MAKE) -C filesystems/mfs -f makefile clean
	$(MAKE) -C storage/ahci -f makefile clean
	$(MAKE) -C timers/hpet -f makefile clean
	$(MAKE) -C serial/usb/ohci -f makefile clean
	$(MAKE) -C serial/usb/uhci -f makefile clean
	rm -rf build