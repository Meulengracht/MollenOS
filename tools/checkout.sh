#!/bin/sh

if [ ! -f $CROSS/bin/clang ]; then
  cd ..
  cd toolchain
  
  if [ ! -f ./llvm ]; then
    svn -q co http://llvm.org/svn/llvm-project/llvm/trunk llvm
    cd llvm/tools
    svn -q co http://llvm.org/svn/llvm-project/cfe/trunk clang
    cd ../..
    
    cd llvm/tools/clang/tools
    svn -q co http://llvm.org/svn/llvm-project/clang-tools-extra/trunk extra
    cd ../../../..
    
    cd llvm/projects
    svn -q co http://llvm.org/svn/llvm-project/compiler-rt/trunk compiler-rt
    svn -q co http://llvm.org/svn/llvm-project/libcxxabi/trunk libcxxabi
    svn -q co http://llvm.org/svn/llvm-project/libcxx/trunk libcxx
    svn -q co http://llvm.org/svn/llvm-project/openmp/trunk openmp
    svn -q co http://llvm.org/svn/llvm-project/lld/trunk lld
    cd ../..
  fi

  cd ..
fi
