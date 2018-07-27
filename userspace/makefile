# Makefile for building all applications and userspace related
#

# Setup environment
export LLVM = $(shell pwd)
export INCLUDES = $(LLVM)/include
export LIBRARIES = $(LLVM)/lib

# Setup userspace environment
# Always build test applications to make sure env is ok.
.PHONY: all
all: bin copy_headers copy_libraries build_zlib build_libpng build_libfreetype build_macia build_cpptest build_wintest

# Build userspace applications, the order might be important.
.PHONY: applications
applications: build_llvm build_mesa build_glm build_vioarr_osmesa

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
	@printf "%b" "\033[1;35mChecking if zlib needs to be built\033[m\n"
	@$(MAKE) -s -C zlib -f makefile

.PHONY: build_libpng
build_libpng:
	@printf "%b" "\033[1;35mChecking if libpng needs to be built\033[m\n"
	@$(MAKE) -s -C libpng -f makefile

.PHONY: build_libfreetype
build_libfreetype:
	@printf "%b" "\033[1;35mChecking if freetype needs to be built\033[m\n"
	@$(MAKE) -s -C freetype -f makefile

.PHONY: build_cpptest
build_cpptest:
	@printf "%b" "\033[1;35mChecking if cpptest needs to be built\033[m\n"
	@$(MAKE) -s -C cpptest -f makefile

.PHONY: build_wintest
build_wintest:
	@printf "%b" "\033[1;35mChecking if wintest needs to be built\033[m\n"
	@$(MAKE) -s -C wintest -f makefile

.PHONY: build_macia
build_macia:
	@printf "%b" "\033[1;35mChecking if macia needs to be built\033[m\n"
	@$(MAKE) -s -C macia -f makefile

.PHONY: build_vioarr_soft
build_vioarr_soft:
	@printf "%b" "\033[1;35mChecking if vioarr needs to be built\033[m\n"
	@$(MAKE) -s -C vioarr -f makefile

llvm_out:
	mkdir -p llvm_out
	cd llvm_out && cmake -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX=$(LLVM) -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=../llvm/cmake/platforms/Vali.cmake ../llvm

.PHONY: build_llvm
build_llvm: llvm_out
	cd llvm_out && make -j3 && make install
	@cp llvm_out/bin/*.lib lib/
	@mv lib/*.dll bin/

.PHONY: build_mesa
build_mesa:
	cd mesa && make && make install

.PHONY: build_glm
build_glm:
	@printf "%b" "\033[1;35mInstalling GLM\033[m\n"
	@cp -r glm/glm/. $(INCLUDES)/glm
	@rm $(INCLUDES)/glm/CMakeLists.txt

.PHONY: build_vioarr_osmesa
build_vioarr_osmesa:
	@printf "%b" "\033[1;35mChecking if vioarr needs to be built\033[m\n"
	@$(MAKE) -s -C vioarr -f makefile osmesa

.PHONY: clean
clean:
	@$(MAKE) -s -C mesa -f makefile clean
	@$(MAKE) -s -C cpptest -f makefile clean
	@$(MAKE) -s -C wintest -f makefile clean
	@$(MAKE) -s -C vioarr -f makefile clean
	@$(MAKE) -s -C libpng -f makefile clean
	@$(MAKE) -s -C freetype -f makefile clean
	@$(MAKE) -s -C zlib -f makefile clean
	@$(MAKE) -s -C macia -f makefile clean
	@rm -rf bin
	@rm -rf include
	@rm -rf lib
	@rm -rf llvm_out
	@rm -rf share