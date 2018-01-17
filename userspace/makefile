# Makefile for building all applications and userspace related
#

.PHONY: all
all: build copy_headers copy_libraries build/cpptest.app

build:
	mkdir -p $@

copy_headers:
	mkdir -p include
	mkdir -p include/ds
	mkdir -p include/os
	mkdir -p include/sys
	mkdir -p include/cxx
	cp ../librt/include/*.h include/
	cp ../librt/include/$(VALI_ARCH)/*.h include/
	cp ../librt/include/sys/*.h include/sys/
	cp ../librt/include/ds/*.h include/ds/
	cp -r ../librt/include/os/ include/
	cp -r ../librt/libcxx/cxx/include/* include/cxx/

copy_libraries:
	mkdir -p lib
	cp ../librt/build/*.lib lib/
	cp ../librt/deploy/*.lib lib/

build/zlib.dll:
	$(MAKE) -C zlib -f makefile

build/cpptest.app:
	$(MAKE) -C cpptest -f makefile

build/llvm.app:
	mkdir -p llvm_out
	cd llvm_out
	cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=../llvm/cmake/platforms/Vali.cmake ../llvm
	make -j3

.PHONY: clean
clean:
	$(MAKE) -C cpptest -f makefile clean
	@rm -rf build
	@rm -rf include
	@rm -rf lib