#!/bin/sh

if [ ! -f $CROSS/bin/clang ]; then
  cd ../toolchain
  cd cross-build
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DLLVM_BUILD_RUNTIME=Off -DLLVM_INCLUDE_TESTS=Off -DLLVM_INCLUDE_EXAMPLES=Off -DLLVM_ENABLE_BACKTRACES=Off -DCMAKE_CROSSCOMPILING=True -DCMAKE_INSTALL_PREFIX=/usr/local/cross -DLLVM_TABLEGEN=llvm-tblgen -DCLANG_TABLEGEN=clang-tblgen -DLLVM_DEFAULT_TARGET_TRIPLE=i386-pc-win32 -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DLLVM_USE_LINKER=lld ../llvm
  make 
  make install
fi
