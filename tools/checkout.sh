#!/bin/sh

if [ ! -f $CROSS/bin/clang ]; then
  cd toolchain
  
  if [ ! -f ./llvm ]; then
    echo Checking out llvm
    svn -q co http://llvm.org/svn/llvm-project/llvm/trunk llvm
    cd llvm/tools
    echo Checking out clang
    svn -q co http://llvm.org/svn/llvm-project/cfe/trunk clang
    cd ../..
    
    cd llvm/tools/clang/tools
    echo Checking out clang-extra
    svn -q co http://llvm.org/svn/llvm-project/clang-tools-extra/trunk extra
    cd ../../../..
    
    cd llvm/projects
    echo Checking out compiler-rt
    svn -q co http://llvm.org/svn/llvm-project/compiler-rt/trunk compiler-rt
    echo Checking out libcxxabi
    svn -q co http://llvm.org/svn/llvm-project/libcxxabi/trunk libcxxabi
    echo Checking out libcxx
    svn -q co http://llvm.org/svn/llvm-project/libcxx/trunk libcxx
    echo Checking out openmp
    svn -q co http://llvm.org/svn/llvm-project/openmp/trunk openmp
    echo Checking out lld
    svn -q co http://llvm.org/svn/llvm-project/lld/trunk lld
    cd ../..
    echo Checkout done
  fi

  cd ..
fi
