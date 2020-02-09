# Primary make-file for building and preparing the OS image
# Required tools for building
# - make gcc g++ nasm mono-complete libelf1 libelf-dev libffi6 libffi-dev flex bison python
# - cmake 3.8+
# - llvm/lld/clang toolchain and cross-compiler
# - clang cross-compiler
# - python + (sudo apt-get install python-pip libyaml-dev) + (sudo pip install prettytable Mako pyaml dateutils --upgrade)
# Required environmental stuff:
# - CROSS=/path/to/cross/home
include config/common.mk

# When building the SDK/DDK we need to have VALI_SDK_PATH defined so we 
# know where it needs to be installed. By default we install in /userspace directory
ifndef VALI_SDK_PATH
VALI_SDK_PATH=$(userspace_path)
endif
ifndef VALI_DDK_PATH
VALI_DDK_PATH=$(userspace_path)
endif

.PHONY: build
build: build_tools gen_revision build_bootloader build_libraries build_kernel build_drivers build_tests

#############################################
##### UTILITY TARGETS                   #####
#############################################
.PHONY: gen_revision_minor
	@printf "%b" "\033[1;35mUpdating revision version (minor)\033[m\n"
	@./revision minor clang

.PHONY: gen_revision
gen_revision:
	@printf "%b" "\033[1;35mUpdating revision file\033[m\n"
	@./revision build clang
	@cp revision.h kernel/include/revision.h

.PHONY: run_bochs
run_bochs:
	 bochs -q -f tools/setup.bochsrc

#############################################
##### BUILD TARGETS (Boot, Lib, Kernel) #####
#############################################
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

.PHONY: build_tests
build_tests:
	@$(MAKE) -s -C tests -f makefile
	
.PHONY: build_tools
build_tools:
	@$(MAKE) -s -C tools/lzss -f makefile
	@$(MAKE) -s -C tools/rd -f makefile
	@$(MAKE) -s -C tools/diskutility -f makefile
	@$(MAKE) -s -C tools/revision -f makefile
	@$(MAKE) -s -C tools/file2c -f makefile

#############################################
##### PACKAGING TARGETS (OS, SDK, DDK)  #####
#############################################
.PHONY: copy_initrd_files
copy_initrd_files:
	@printf "%b" "\033[1;35mPackaging initrd files\033[m\n"
	@mkdir -p initrd
	@cp librt/build/*.dll initrd/
	@cp services/build/*.dll initrd/
	@cp services/build/*.mdrv initrd/
	@cp modules/build/*.dll initrd/
	@cp modules/build/*.mdrv initrd/
	#@cp resources/initrd/* initrd/

.PHONY: package_os
package_os: copy_initrd_files
	$(eval VALI_VERSION = $(shell ./revision print all))
	@mkdir -p os_package
	@cp -a boot/build/. os_package/
	@./rd $(VALI_ARCH) initrd.mos
	@./lzss c initrd.mos os_package/initrd.mos
	@./lzss c kernel/build/syskrnl.mos os_package/syskrnl.mos
	@cp -r resources os_package/
	@cp diskutility os_package/
	@cp DiscUtils* os_package/
	@cd os_package; zip -r vali-$(VALI_VERSION)-$(VALI_ARCH).zip .
	@mv os_package/vali-$(VALI_VERSION)-$(VALI_ARCH).zip .
	@rm -rf os_package
	@rm initrd.mos

.PHONY: package_sdk
package_sdk: package_sdk_headers package_sdk_libraries
	$(eval VALI_VERSION = $(shell ./revision print all))
	@cd $(VALI_SDK_PATH); zip -r vali-sdk-$(VALI_VERSION)-$(VALI_ARCH).zip .
	@mv $(VALI_SDK_PATH)/vali-sdk-$(VALI_VERSION)-$(VALI_ARCH).zip .
	@rm -rf $(VALI_SDK_PATH)

.PHONY: package_sdk_headers
package_sdk_headers:
	@mkdir -p $(VALI_SDK_PATH)/include
	@mkdir -p $(VALI_SDK_PATH)/include/ds
	@mkdir -p $(VALI_SDK_PATH)/include/os
	@mkdir -p $(VALI_SDK_PATH)/include/inet
	@mkdir -p $(VALI_SDK_PATH)/include/sys
	@mkdir -p $(VALI_SDK_PATH)/include/cxx
	@cp librt/include/*.h $(VALI_SDK_PATH)/include/
	@cp librt/libc/include/*.h $(VALI_SDK_PATH)/include/
	@cp -r librt/libc/include/os/ $(VALI_SDK_PATH)/include/
	@cp librt/libc/include/inet/*.h $(VALI_SDK_PATH)/include/inet/
	@cp librt/libc/include/sys/*.h $(VALI_SDK_PATH)/include/sys/
	@cp librt/libm/include/*.h $(VALI_SDK_PATH)/include/
	@cp librt/libds/include/ds/*.h $(VALI_SDK_PATH)/include/ds/
	@cp -r librt/libcxx/libcxx/include/* $(VALI_SDK_PATH)/include/cxx/

.PHONY: package_sdk_libraries
package_sdk_libraries:
	@mkdir -p $(VALI_SDK_PATH)/bin
	@mkdir -p $(VALI_SDK_PATH)/lib
	@cp librt/build/*.dll $(VALI_SDK_PATH)/bin/
	@cp librt/build/*.lib $(VALI_SDK_PATH)/lib/
	@cp librt/deploy/*.dll $(VALI_SDK_PATH)/bin/
	@cp librt/deploy/*.lib $(VALI_SDK_PATH)/lib/

.PHONY: package_ddk
package_ddk: package_ddk_headers
	$(eval VALI_VERSION = $(shell ./revision print all))
	@cd $(VALI_DDK_PATH); zip -r vali-ddk-$(VALI_VERSION)-$(VALI_ARCH).zip .
	@mv $(VALI_DDK_PATH)/vali-ddk-$(VALI_VERSION)-$(VALI_ARCH).zip .
	@rm -rf $(VALI_DDK_PATH)

.PHONY: package_ddk_headers
package_ddk_headers:
	@mkdir -p $(VALI_DDK_PATH)/include
	@mkdir -p $(VALI_DDK_PATH)/include/ddk
	@mkdir -p $(VALI_DDK_PATH)/include/wm
	@mkdir -p $(VALI_DDK_PATH)/include/protocols
	@cp -r librt/libddk/include/ddk/* $(VALI_DDK_PATH)/include/ddk/
	@cp -r librt/libwm/include/* $(VALI_DDK_PATH)/include/wm/
	@cp -r protocols/* $(VALI_DDK_PATH)/include/protocols/

#############################################
##### INSTALL/IMAGE TARGETS             #####
#############################################
.PHONY: install_sdk
install_sdk: package_sdk_headers package_sdk_libraries

.PHONY: install_ddk
install_ddk: package_ddk_headers

# Build the deploy directory, which contains the primary (system) drive
# structure, system folder, default binaries etc
.PHONY: install_shared
install_shared: copy_initrd_files
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
	if [ -d $(VALI_APPLICATION_PATH) ]; then \
        cp $(VALI_APPLICATION_PATH)/bin/*.app deploy/hdd/shared/bin/ 2>/dev/null || :; \
        cp $(VALI_APPLICATION_PATH)/bin/*.dll deploy/hdd/shared/bin/ 2>/dev/null || :; \
    else \
	    cp tests/bin/*.app deploy/hdd/shared/bin/ 2>/dev/null || :; \
	    cp tests/bin/*.dll deploy/hdd/shared/bin/ 2>/dev/null || :; \
    fi

.PHONY: install_img
install_img: install_shared
	mono diskutility -auto -target img -scheme mbr
	cp mollenos.img mollenos_usb.img

.PHONY: install_vmdk
install_vmdk: install_shared
	mono diskutility -auto -target vmdk -scheme mbr

.PHONY: clean
clean:
	@$(MAKE) -s -C boot -f makefile clean
	@$(MAKE) -s -C librt -f makefile clean
	@$(MAKE) -s -C services -f makefile clean
	@$(MAKE) -s -C modules -f makefile clean
	@$(MAKE) -s -C kernel -f makefile clean
	@$(MAKE) -s -C tools/lzss -f makefile clean
	@$(MAKE) -s -C tools/rd -f makefile clean
	@$(MAKE) -s -C tools/diskutility -f makefile clean
	@$(MAKE) -s -C tools/revision -f makefile clean
	@$(MAKE) -s -C tools/file2c -f makefile clean
	@$(MAKE) -s -C tests -f makefile clean
	@rm -f kernel/include/revision.h
	@rm -f initrd.mos
	@rm -rf deploy
	@rm -rf initrd
	@rm -f *.vmdk
	@rm -f *.img
	@rm -f *.lock
	@rm -f *.zip
