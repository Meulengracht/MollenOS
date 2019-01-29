#!/bin/sh

# toolchain directory is required
clear
mkdir -p toolchain
cd toolchain

# Dev-libraries
sudo apt-get -qq install libelf1 libelf-dev libffi6 libffi-dev make gcc g++ git nasm mono-complete flex bison python python-pip libyaml-dev
pip install prettytable Mako pyaml dateutils --upgrade

# cmake version 3 is required
CMAKE_VERSION="$(cmake --version)"
CMAKE_REQUIRED_VERSION="1.13.0"
if [ ! "$(printf '%s\n' "$CMAKE_REQUIRED_VERSION" "$CMAKE_VERSION" | sort -V | head -n1)" = "$CMAKE_REQUIRED_VERSION" ]; then
  if [ ! -f ./cmake-3.13.0-rc3 ]; then
    wget https://cmake.org/files/v3.13/cmake-3.13.0-rc3.tar.gz
    tar xzf cmake-3.13.0-rc3.tar.gz
    rm cmake-3.13.0-rc3.tar.gz
  fi
  cd cmake-3.13.0-rc3
  if [ -x "$(command -v cmake)" ]; then
    apt-get -qq remove "^cmake.*" 
  fi
  ./bootstrap
  make
  sudo make install
  cd ..
fi

# go out of toolchain
cd ..

# Install the cmake platform template
cp ./Vali.cmake /usr/local/share/cmake-3.13/Modules/Platform/Vali.cmake
