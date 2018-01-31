# Makefile for building the mollenos services
# Includes services that run both user-space and kernel apis
# - devices
# - files
# - sessions
# - usb
# - windows

.PHONY: all
all: build devicemanager filemanager usbmanager sessionmanager

build:
	@mkdir -p $@

.PHONY: devicemanager
devicemanager:
	@$(MAKE) -s -C devicemanager -f makefile

.PHONY: filemanager
filemanager:
	@$(MAKE) -s -C filemanager -f makefile

.PHONY: sessionmanager
sessionmanager:
	@$(MAKE) -s -C sessionmanager -f makefile

.PHONY: usbmanager
usbmanager:
	@$(MAKE) -s -C usbmanager -f makefile

.PHONY: windowmanager
windowmanager:
	@$(MAKE) -s -C windowmanager -f makefile

.PHONY: clean
clean:
	@$(MAKE) -s -C sessionmanager -f makefile clean
	@$(MAKE) -s -C devicemanager -f makefile clean
	@$(MAKE) -s -C filemanager -f makefile clean
	@$(MAKE) -s -C usbmanager -f makefile clean
	@rm -rf build