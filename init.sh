#!/bin/sh

if [ ! -f /usr/local/cross/bin/clang ]; then
  clear
  mkdir -p toolchain
  cd toolchain
  
  if ! [ -x "$(command -v svn)" ]; then
    apt-get install subversion
  fi
  
  CMAKE_VERSION="$(cmake --version)"
  echo ${CMAKE_VERSION}
  if ! [[ "$CMAKE_VERSION" =~ "cmake version 3" ]]; then
    wget https://cmake.org/files/v3.8/cmake-3.8.1.tar.gz
    tar xvzf cmake-3.8.1.tar.gz
    rm cmake-3.8.1.tar.gz
    cd cmake-3.8.1
	if ! [ -x "$(command -v cmake)" ]; then
      ./bootstrap 
	else
	  cmake .
    fi
    make
    make install
    cd ..
  fi
  
  if ! [ -x "$(command -v clang)" ]; then
    if [ ! -f ./llvm ]; then
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
	fi
    
	mkdir -p build
	cd build
    cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_EH=True -DLLVM_ENABLE_RTTI=True  ../llvm
    make 
    make install
	cd ..
  fi

  mkdir -p /usr/local/cross
  mkdir -p cross-build
  cd cross-build
  cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DLLVM_BUILD_RUNTIME=Off -DLLVM_INCLUDE_TESTS=Off -DLLVM_INCLUDE_EXAMPLES=Off -DLLVM_ENABLE_BACKTRACES=Off -DCMAKE_CROSSCOMPILING=True -DCMAKE_INSTALL_PREFIX=/usr/local/cross -DLLVM_TABLEGEN=llvm-tblgen -DCLANG_TABLEGEN=clang-tblgen -DLLVM_DEFAULT_TARGET_TRIPLE=i386-pc-win32 -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DLLVM_USE_LINKER=lld ../llvm
  make 
  make install
fi
