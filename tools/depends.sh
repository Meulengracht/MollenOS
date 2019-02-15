#!/bin/bash

# Dev-libraries
sudo apt-get -qq install nasm mono-complete

# Install the cmake platform template
sudo cp ./Vali.cmake /usr/local/share/cmake-3.13/Modules/Platform/Vali.cmake
