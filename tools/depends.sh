#!/bin/bash

# Dev-libraries
sudo apt-get -qq install libelf1 libelf-dev libffi6 libffi-dev make gcc g++ git nasm mono-complete flex bison python python-pip libyaml-dev
pip install prettytable Mako pyaml dateutils --upgrade

# cmake version 3 is required
CMAKE_VERSION="$(cmake --version)";
if [[ $CMAKE_VERSION != *"3.13.0"* ]]; then
  mkdir -p cmake
  cd cmake
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

# Install the cmake platform template
cp ./Vali.cmake /usr/local/share/cmake-3.13/Modules/Platform/Vali.cmake
