#!/bin/sh

# toolchain directory is required
clear
mkdir -p toolchain
cd toolchain

# svn is required
if ! [ -x "$(command -v svn)" ]; then
  apt-get install subversion
fi

# nasm is required
if ! [ -x "$(command -v nasm)" ]; then
  apt-get install nasm
fi

# mono is required
if ! [ -x "$(command -v mono)" ]; then
  apt-get install monodevelop
fi

# cmake version 3 is required
CMAKE_VERSION="$(cmake --version)"
echo ${CMAKE_VERSION}
if ! [[ "$CMAKE_VERSION" =~ "cmake version 3" ]]; then
  wget https://cmake.org/files/v3.8/cmake-3.8.1.tar.gz
  tar xvzf cmake-3.8.1.tar.gz
  rm cmake-3.8.1.tar.gz
  cd cmake-3.8.1
  if [ -x "$(command -v cmake)" ]; then
    apt-get remove "^cmake.*" 
  fi
  ./bootstrap
  make
  make install
  cd ..
fi

# go out of toolchain
cd ..