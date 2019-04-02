# Makefile for building the standard c++abi runtime-libraries for userspace
# This will produce the file libcxxabi.lib

# Include all the definitions for os
include ../../config/common.mk

export VALI_INCLUDES := -I$(workspace_path)/librt/libddk/include -I$(workspace_path)/librt/libc/include -I$(workspace_path)/librt/include
export VALI_LIBRARIES := -LIBPATH:$(workspace_path)/librt/build
export VALI_LFLAGS := $(GLFLAGS) /lldmap $(VALI_LIBRARIES)

.PHONY: all
all: build
	cd build && make
	cp build/lib/* ../deploy/

build:
	mkdir -p $@
	cd $@ && cmake -DCMAKE_BUILD_TYPE=Release ..

.PHONY: clean
clean:
	@rm -rf build
	