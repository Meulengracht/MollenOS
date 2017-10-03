#!/bin/sh

if [ ! -f $CROSS/bin/clang ]; then
  cd ../toolchain
  
  if ! [ -x "$(command -v clang)" ]; then
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
    
	mkdir -p build
  fi

  mkdir -p $CROSS
  mkdir -p cross-build
fi
