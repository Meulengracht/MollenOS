#!/bin/sh

if [ ! -f $CROSS/bin/clang ]; then
  cd toolchain
  
  if [ ! -f ./llvm ]; then
    echo Checking out llvm
    git clone https://github.com/Meulengracht/llvm.git
    cd llvm/tools
    echo Checking out clang
    git clone https://github.com/Meulengracht/clang.git
    cd ../..
    
    cd llvm/tools/clang/tools
    echo Checking out clang-extra
    git clone https://github.com/Meulengracht/clang-tools-extra.git extra
    cd ../../../..
    
    cd llvm/projects
    echo Checking out compiler-rt
    git clone https://github.com/Meulengracht/compiler-rt.git
    echo Checking out libcxxabi
    git clone https://github.com/Meulengracht/libcxxabi.git
    echo Checking out libcxx
    git clone https://github.com/Meulengracht/libcxx.git
    echo Checking out openmp
    git clone https://github.com/Meulengracht/openmp.git
    echo Checking out lld
    git clone https://github.com/Meulengracht/lld.git
    cd ../..
    echo Checkout done
  fi

  cd ..
fi
