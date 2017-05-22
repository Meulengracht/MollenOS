#!/bin/sh

clear
mkdir toolchain
cd toolchain

if ! [ -x "$(command -v svn)" ]; then
  apt-get install svn
fi

if ! [ -x "$(command -v cmake)" ]; then
  wget https://cmake.org/files/v3.8/cmake-3.8.1.tar.gz
  tar xvzf cmake-3.8.1.tar.gz
  rm cmake-3.8.1.tar.gz
  cd cmake-3.8.1
  make
  make install
  cd ..
fi

svn co http://llvm.org/svn/llvm-project/llvm/trunk llvm
cd llvm/tools
svn co http://llvm.org/svn/llvm-project/cfe/trunk clang
cd ../..

cd llvm/tools/clang/tools
svn co http://llvm.org/svn/llvm-project/clang-tools-extra/trunk extra
cd ../../../..

cd llvm/projects
svn co http://llvm.org/svn/llvm-project/compiler-rt/trunk compiler-rt
svn co http://llvm.org/svn/llvm-project/libcxxabi/trunk libcxxabi
svn co http://llvm.org/svn/llvm-project/libcxx/trunk libcxx
svn co http://llvm.org/svn/llvm-project/openmp/trunk openmp
cd ../..

mkdir cross-bin
mkdir build
mkdir cross-build
cd build
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_EH=True -DLLVM_ENABLE_RTTI=True  ../llvm
make 
make install
cd ..
cd cross-build
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DLLVM_BUILD_RUNTIME=Off -DLLVM_INCLUDE_TESTS=Off -DLLVM_INCLUDE_EXAMPLES=Off -DLLVM_ENABLE_BACKTRACES=Off -DCMAKE_CROSSCOMPILING=True -DCMAKE_INSTALL_PREFIX=../cross-bin -DLLVM_TABLEGEN=llvm-tblgen -DCLANG_TABLEGEN=clang-tblgen -DLLVM_DEFAULT_TARGET_TRIPLE=i386-pc-win32 -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DLLVM_USE_LINKER=lld ../llvm
make 
make install