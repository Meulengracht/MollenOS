# Makefile for building all applications and userspace related
#

.PHONY: all
all: build headers libraries build/cpptest.app

build:
	mkdir -p $@

headers:
	mkdir -p include
	mkdir -p include/ds
	mkdir -p include/os
	mkdir -p include/sys
	mkdir -p include/cxx
	$(FCOPY) ../librt/include/*.h include/
	$(FCOPY) ../librt/include/$(arch)/*.h include/
	$(FCOPY) ../librt/include/sys/*.h include/sys/
	$(FCOPY) ../librt/include/ds/*.h include/ds/
	$(FCOPY) -r ../librt/include/os/ include/
	$(FCOPY) -r ../librt/include/cxx/ include/

libraries:
	mkdir -p lib
	$(FCOPY) ../librt/build/*.lib lib/
	$(FCOPY) ../librt/deploy/*.lib lib/

build/cpptest.app:
	$(MAKE) -C cpptest -f makefile

.PHONY: clean
clean:
	$(MAKE) -C cpptest -f makefile clean
	@rm -rf build