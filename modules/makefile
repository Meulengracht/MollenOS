# Makefile for building the mollenos drivers
# Includes drivers that run user-space apis
# - drivers

.PHONY: all
all: build $(VALI_ARCH) mfs ahci usb ehci uhci ohci msd hid

build:
	@mkdir -p $@

# Architecture specific drivers
.PHONY: i386
i386:
	@$(MAKE) -s -C arch/x86/ps2 -f makefile

.PHONY: amd64
amd64:
	@$(MAKE) -s -C arch/x86/ps2 -f makefile

# Shared architecture targets below
.PHONY: mfs
mfs:
	@$(MAKE) -s -C filesystems/mfs -f makefile

.PHONY: ahci
ahci:
	@$(MAKE) -s -C storage/ahci -f makefile

.PHONY: msd
msd:
	@$(MAKE) -s -C storage/msd -f makefile

.PHONY: hid
hid:
	@$(MAKE) -s -C input/hid -f makefile

# Usb drivers below here
.PHONY: usb
usb:
	@$(MAKE) -s -C serial/usb/common -f makefile

.PHONY: ohci
ohci:
	@$(MAKE) -s -C serial/usb/ohci -f makefile

.PHONY: uhci
uhci:
	@$(MAKE) -s -C serial/usb/uhci -f makefile

.PHONY: ehci
ehci:
	@$(MAKE) -s -C serial/usb/ehci -f makefile

.PHONY: xhci
xhci:
	@$(MAKE) -s -C serial/usb/xhci -f makefile

# Cleanup - architecture i386
.PHONY: clean_i386
clean_i386:
	@$(MAKE) -s -C arch/x86/ps2 -f makefile clean

# Cleanup - architecture amd64
.PHONY: clean_amd64
clean_amd64:
	@$(MAKE) -s -C arch/x86/ps2 -f makefile clean

# Cleanup - shared
.PHONY: clean
clean: clean_$(VALI_ARCH)
	@$(MAKE) -s -C filesystems/mfs -f makefile clean
	@$(MAKE) -s -C storage/ahci -f makefile clean
	@$(MAKE) -s -C storage/msd -f makefile clean
	@$(MAKE) -s -C input/hid -f makefile clean
	@$(MAKE) -s -C serial/usb/common -f makefile clean
	@$(MAKE) -s -C serial/usb/ohci -f makefile clean
	@$(MAKE) -s -C serial/usb/uhci -f makefile clean
	@$(MAKE) -s -C serial/usb/ehci -f makefile clean
	@rm -rf build