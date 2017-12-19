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
	cp ../librt/include/*.h include/
	cp ../librt/include/$(arch)/*.h include/
	cp ../librt/include/sys/*.h include/sys/
	cp ../librt/include/ds/*.h include/ds/
	cp -r ../librt/include/os/ include/
	cp -r ../librt/include/cxx/ include/

libraries:
	mkdir -p lib
	cp ../librt/build/*.lib lib/
	cp ../librt/deploy/*.lib lib/

build/cpptest.app:
	$(MAKE) -C cpptest -f makefile

.PHONY: clean
clean:
	$(MAKE) -C cpptest -f makefile clean
	@rm -rf build