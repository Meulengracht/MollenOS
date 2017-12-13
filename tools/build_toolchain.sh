#!/bin/sh

if [ ! -f $CROSS/bin/clang ]; then
  cd toolchain

  # cmake version 3 is required
  CMAKE_VERSION="$(cmake --version)"
  echo ${CMAKE_VERSION}
  if ! [[ "$CMAKE_VERSION" =~ "cmake version 3.8" ]]; then
    if [ ! -f ./cmake-3.8.1 ]; then
      wget https://cmake.org/files/v3.8/cmake-3.8.1.tar.gz
      tar xzf cmake-3.8.1.tar.gz
      rm cmake-3.8.1.tar.gz
    fi
    cd cmake-3.8.1
    if [ -x "$(command -v cmake)" ]; then
      apt-get -qq remove "^cmake.*" 
    fi
    ./bootstrap
    make
    make install
    cd ..
  fi
  mkdir -p $CROSS
  mkdir -p cross-build
  cd cross-build
  cmake -G "Unix Makefiles" -DLLVM_ENABLE_EH=True -DLLVM_ENABLE_RTTI=True -DCLANG_DEFAULT_RTLIB=compiler-rt -DCLANG_DEFAULT_CXX_STDLIB=libc++ -DCMAKE_BUILD_TYPE=Release -DLLVM_INCLUDE_TESTS=Off -DLLVM_INCLUDE_EXAMPLES=Off -DCMAKE_CROSSCOMPILING=True -DCMAKE_INSTALL_PREFIX=$CROSS -DLLVM_TABLEGEN=llvm-tblgen -DCLANG_TABLEGEN=clang-tblgen -DLLVM_DEFAULT_TARGET_TRIPLE=i386-pc-vali-coff -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DLLVM_USE_LINKER=lld ../llvm
  make 
  make install
fi
