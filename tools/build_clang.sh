#!/bin/sh

if [ ! -f $CROSS/bin/clang ]; then
  cd toolchain
  
  if ! [ -x "$(command -v clang)" ]; then
	mkdir -p build
	cd build
    cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_EH=True -DLLVM_ENABLE_RTTI=True  ../llvm
    make 
    make install
	cd ..
  fi
fi
