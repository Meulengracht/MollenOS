#!/bin/sh

if [ ! -f $CROSS/bin/clang ]; then
  cd toolchain
  
  if [ ! -f ./llvm ]; then
    echo Checking out llvm
    git clone https://github.com/llvm-mirror/llvm.git
    cd llvm/tools
    echo Checking out clang
    git clone https://github.com/llvm-mirror/clang.git
    cd ../..
    
    cd llvm/tools/clang/tools
    echo Checking out clang-extra
    git clone https://github.com/llvm-mirror/clang-tools-extra.git extra
    cd ../../../..
    
    cd llvm/projects
    echo Checking out compiler-rt
    git clone https://github.com/llvm-mirror/compiler-rt.git
    echo Checking out libcxxabi
    git clone https://github.com/llvm-mirror/libcxxabi.git
    echo Checking out libcxx
    git clone https://github.com/llvm-mirror/libcxx.git
    echo Checking out openmp
    git clone https://github.com/llvm-mirror/openmp.git
    echo Checking out lld
    git clone https://github.com/llvm-mirror/lld.git
    cd ../..
    echo Checkout done
  fi

  cd ..
fi
