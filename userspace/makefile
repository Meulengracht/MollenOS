# Makefile for building all applications and userspace related
#

# Setup environment
install_prefix = $(shell pwd)
export INCLUDES = $(install_prefix)/include
export LIBRARIES = $(install_prefix)/lib

# Setup userspace environment
# Always build test applications to make sure env is ok.
.PHONY: all
all: bin copy_headers copy_libraries build_cpptest

# Build userspace applications, the order might be important.
.PHONY: applications
applications: build_zlib build_macia build_llvm

bin:
	@mkdir -p $@

.PHONY: copy_headers
copy_headers:
	@mkdir -p include
	@mkdir -p include/ds
	@mkdir -p include/os
	@mkdir -p include/sys
	@mkdir -p include/cxx
	@cp ../librt/include/*.h include/
	@cp ../librt/include/$(VALI_ARCH)/*.h include/
	@cp ../librt/include/sys/*.h include/sys/
	@cp ../librt/include/ds/*.h include/ds/
	@cp -r ../librt/include/os/ include/
	@cp -r ../librt/libcxx/cxx/include/* include/cxx/

.PHONY: copy_libraries
copy_libraries:
	@mkdir -p lib
	@cp ../librt/build/*.lib lib/
	@cp ../librt/deploy/*.lib lib/

.PHONY: build_zlib
build_zlib:
	$(MAKE) -C zlib -f makefile

.PHONY: build_cpptest
build_cpptest:
	$(MAKE) -C cpptest -f makefile

.PHONY: build_macia
build_macia:
	$(MAKE) -C macia -f makefile

.PHONY: build_llvm
build_llvm:
	mkdir -p llvm_out
	cd llvm_out; \
	cmake -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX=$(install_prefix) -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=../llvm/cmake/platforms/Vali.cmake ../llvm; \
	make; \
	make install

.PHONY: clean
clean:
	$(MAKE) -C cpptest -f makefile clean
	$(MAKE) -C zlib -f makefile clean
	@rm -rf bin
	@rm -rf include
	@rm -rf lib
	@rm -rf llvm_out