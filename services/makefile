# Makefile for building the mollenos services
# Includes services that run both user-space and kernel apis
# - devices
# - files
# - sessions
# - usb
# - windows

.PHONY: all
all: build devicemanager filemanager usbmanager

build:
	@mkdir -p $@

.PHONY: devicemanager
devicemanager:
	$(MAKE) -C devicemanager -f makefile

.PHONY: filemanager
filemanager:
	$(MAKE) -C filemanager -f makefile

.PHONY: sessionmanager
sessionmanager:
	$(MAKE) -C sessionmanager -f makefile

.PHONY: usbmanager
usbmanager:
	$(MAKE) -C usbmanager -f makefile

.PHONY: windowmanager
windowmanager:
	$(MAKE) -C windowmanager -f makefile

.PHONY: clean
clean:
	$(MAKE) -C devicemanager -f makefile clean
	$(MAKE) -C filemanager -f makefile clean
	$(MAKE) -C usbmanager -f makefile clean
	rm -rf build