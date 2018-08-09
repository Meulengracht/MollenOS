# Primary make-file for building and preparing the OS image
# Required tools for building
# - make gcc g++ nasm mono-complete libelf1 libelf-dev libffi6 libffi-dev flex bison python
# - cmake 3.8+
# - llvm/lld/clang toolchain and cross-compiler
# - clang cross-compiler
# - python + (sudo apt-get install python-pip libyaml-dev) + (sudo pip install prettytable Mako pyaml dateutils --upgrade)
# Required environmental stuff:
# - CROSS=/path/to/cross/home
#

# Include all the definitions for os
include config/common.mk

.PHONY: all
all: build_tools gen_revision build_bootloader build_libraries build_kernel build_drivers setup_userspace build_initrd

.PHONY: tidy
tidy: tidy_libraries tidy_kernel

#kernel/git_revision.c: .git/HEAD .git/index
#    echo "const char *gitversion = \"$(shell git rev-parse HEAD)\";" > $@

# Kernel + minimal userspace release
.PHONY: kernel_release
kernel_release: build_tools gen_revision_minor gen_revision build_bootloader build_libraries build_kernel build_drivers setup_userspace build_initrd install_img
	#zip mollenos.img mollenos_$(shell revision print all).zip
	#zip userspace/include mollenos_$(shell revision print all)_sdk.zip
	#zip userspace/lib mollenos_$(shell revision print all)_sdk.zip
	#zip userspace/bin mollenos_$(shell revision print all)_sdk.zip

.PHONY: build_initrd
build_initrd:
	@printf "%b" "\033[1;35mInstalling initrd files into /initrd\033[m\n"
	@mkdir -p initrd
	@cp librt/build/*.dll initrd/
	@cp services/build/*.dll initrd/
	@cp services/build/*.mdrv initrd/
	@cp modules/build/*.dll initrd/
	@cp modules/build/*.mdrv initrd/
	#cp resources/initrd/* initrd/

.PHONY: build_tools
build_tools:
	@$(MAKE) -s -C tools/lzss -f makefile
	@$(MAKE) -s -C tools/rd -f makefile
	@$(MAKE) -s -C tools/diskutility -f makefile
	@$(MAKE) -s -C tools/revision -f makefile
	@$(MAKE) -s -C tools/file2c -f makefile

.PHONY: gen_revision_minor
	@printf "%b" "\033[1;35mUpdating revision version (minor)\033[m\n"
	@./revision minor clang

.PHONY: gen_revision
gen_revision:
	@printf "%b" "\033[1;35mUpdating revision file\033[m\n"
	@./revision build clang
	@cp revision.h kernel/include/revision.h

.PHONY: setup_userspace
setup_userspace:
	@printf "%b" "\033[1;35mSetting up userspace folders(include/lib)\033[m\n"
	@$(MAKE) -s -C userspace -f makefile

.PHONY: build_userspace
build_userspace:
	@$(MAKE) -s -C userspace -f makefile applications

.PHONY: build_kernel
build_kernel:
	@$(MAKE) -s -C kernel -f makefile

.PHONY: build_drivers
build_drivers:
	@$(MAKE) -s -C services -f makefile
	@$(MAKE) -s -C modules -f makefile

.PHONY: build_libraries
build_libraries:
	@$(MAKE) -s -C librt -f makefile

.PHONY: build_bootloader
build_bootloader:
	@$(MAKE) -s -C boot -f makefile

# Tidy targets for static code analysis using tool like clang-tidy
# these targets do not build anything, and we only clang-tidy our os-code
.PHONY: tidy_kernel
tidy_kernel:
	@$(ANALYZER) $(MAKE) -s -C kernel -f makefile tidy

.PHONY: tidy_drivers
tidy_drivers:
	@$(ANALYZER) $(MAKE) -s -C services -f makefile tidy
	@$(ANALYZER) $(MAKE) -s -C modules -f makefile tidy

.PHONY: tidy_libraries
tidy_libraries:
	@$(ANALYZER) $(MAKE) -s -C librt -f makefile tidy

# Build the deploy directory, which contains the primary (system) drive
# structure, system folder, default binaries etc
.PHONY: install_shared
install_shared:
	mkdir -p deploy/hdd
	mkdir -p deploy/hdd/shared
	mkdir -p deploy/hdd/shared/bin
	mkdir -p deploy/hdd/system
	cp -a resources/system/. deploy/hdd/system/
	cp -a resources/shared/. deploy/hdd/shared/
	cp -a boot/build/. deploy/
	./rd $(VALI_ARCH) initrd.mos
	./lzss c initrd.mos deploy/hdd/system/initrd.mos
	./lzss c kernel/build/syskrnl.mos deploy/hdd/system/syskrnl.mos
	cp librt/build/*.lib deploy/hdd/shared/lib/
	cp librt/build/*.dll deploy/hdd/shared/bin/
	cp librt/deploy/*.lib deploy/hdd/shared/lib/
	cp librt/deploy/*.dll deploy/hdd/shared/bin/
	cp userspace/bin/*.app deploy/hdd/shared/bin/ 2>/dev/null || :
	cp userspace/bin/*.dll deploy/hdd/shared/bin/ 2>/dev/null || :
	#cp userspace/lib/* deploy/hdd/shared/lib/ 2>/dev/null || :
	#cp userspace/include/* deploy/hdd/shared/include/ 2>/dev/null || :

.PHONY: install_img
install_img: install_shared
	mono diskutility -auto -target img -scheme mbr
	cp mollenos.img mollenos_usb.img

.PHONY: install_vmdk
install_vmdk: install_shared
	mono diskutility -auto -target vmdk -scheme mbr

.PHONY: build_toolchain
build_toolchain:
	mkdir -p toolchain
	chmod +x ./tools/depends.sh
	chmod +x ./tools/checkout.sh
	chmod +x ./tools/build_clang.sh
	chmod +x ./tools/build_toolchain.sh
	bash ./tools/depends.sh
	bash ./tools/checkout.sh
	bash ./tools/build_clang.sh
	bash ./tools/build_toolchain.sh
	rm -rf toolchain

.PHONY: clean
clean:
	@$(MAKE) -s -C boot -f makefile clean
	@$(MAKE) -s -C librt -f makefile clean
	@$(MAKE) -s -C services -f makefile clean
	@$(MAKE) -s -C modules -f makefile clean
	@$(MAKE) -s -C kernel -f makefile clean
	@$(MAKE) -s -C userspace -f makefile clean
	@$(MAKE) -s -C tools/lzss -f makefile clean
	@$(MAKE) -s -C tools/rd -f makefile clean
	@$(MAKE) -s -C tools/diskutility -f makefile clean
	@$(MAKE) -s -C tools/revision -f makefile clean
	@$(MAKE) -s -C tools/file2c -f makefile clean
	@rm -f initrd.mos
	@rm -rf deploy
	@rm -rf initrd
	@rm -f *.vmdk
	@rm -f *.img
	@rm -f *.lock
