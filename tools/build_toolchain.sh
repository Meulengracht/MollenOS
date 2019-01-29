#!/bin/sh

if [ ! -f $CROSS/bin/clang ]; then
  cd toolchain
  sudo mkdir -p $CROSS
  mkdir -p cross-build
  cd cross-build
  cmake -G "Unix Makefiles" -DLLVM_ENABLE_EH=True -DLLVM_ENABLE_RTTI=True -DCLANG_DEFAULT_RTLIB=compiler-rt -DCLANG_DEFAULT_CXX_STDLIB=libc++ -DCMAKE_BUILD_TYPE=Release -DLLVM_INCLUDE_TESTS=Off -DLLVM_INCLUDE_EXAMPLES=Off -DCMAKE_CROSSCOMPILING=True -DCMAKE_INSTALL_PREFIX=$CROSS -DLLVM_TABLEGEN=llvm-tblgen -DCLANG_TABLEGEN=clang-tblgen -DLLVM_DEFAULT_TARGET_TRIPLE=i386-pc-win32-itanium-coff -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DLLVM_USE_LINKER=lld ../llvm
  make 
  sudo make install
fi
