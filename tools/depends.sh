#!/bin/sh

# toolchain directory is required
clear
cd toolchain

# Dev-libraries
apt-get -qq install libelf1
apt-get -qq install libelf-dev
apt-get -qq install libffi6
apt-get -qq install libffi-dev

# make is required
if ! [ -x "$(command -v make)" ]; then
  apt-get -qq install make
fi

# gcc is required
if ! [ -x "$(command -v gcc)" ]; then
  apt-get -qq install gcc
fi

# g++ is required
if ! [ -x "$(command -v g++)" ]; then
  apt-get -qq install g++
fi

# svn is required
if ! [ -x "$(command -v svn)" ]; then
  apt-get -qq install subversion
fi

# nasm is required
if ! [ -x "$(command -v nasm)" ]; then
  apt-get -qq install nasm
fi

# mono is required
if ! [ -x "$(command -v mono)" ]; then
  apt-get -qq install mono-complete
fi

# cmake version 3 is required
CMAKE_VERSION="$(cmake --version)"
echo ${CMAKE_VERSION}
if ! [[ "$CMAKE_VERSION" =~ "cmake version 3.8" ]]; then
  if [ ! -f ./cmake-3.8.1 ]; then
    wget https://cmake.org/files/v3.8/cmake-3.8.1.tar.gz
    tar xzf cmake-3.8.1.tar.gz
    rm cmake-3.8.1.tar.gz
  fi
  cd cmake-3.8.1
  if [ -x "$(command -v cmake)" ]; then
    apt-get -qq remove "^cmake.*" 
  fi
  ./bootstrap
  make
  make install
  cd ..
fi

# go out of toolchain
cd ..