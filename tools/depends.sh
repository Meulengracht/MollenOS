#!/bin/bash
SCRIPT=`realpath $0`
SCRIPTPATH=`dirname $SCRIPT`

# Dev-libraries
echo "** installing build dependencies"
apt-get update -yqq
apt-get -y -qq install git cmake gcc g++ zip nasm make python python3 snapd

# Install dotnet core
if ! [ -x "$(command -v dotnet)" ]; then
  echo "** installing dotnet core"
  snap install dotnet-sdk --classic --channel=lts/stable # 3.1
  snap install dotnet-runtime-31 --classic # 3.1
  snap alias dotnet-runtime-31.dotnet dotnet
  echo "*************************************************"
  echo "** DOTNET CORE HAS BEEN INSTALLED              **"
  echo "** PLEASE ADD THIS TO YOUR ~/.profile          **"
  echo "** export DOTNET_ROOT=/snap/dotnet-sdk/current **"
  echo "*************************************************"
fi

# Install the cmake platform template
echo "** configuring platform cmake"
CMAKE_VERSION="$(cmake --version)";
CMAKE_REGEX="cmake version ([0-9]+\.[0-9]+)\.[0-9]+"
if [[ $CMAKE_VERSION =~ $CMAKE_REGEX ]]
    then
        regex_match="${BASH_REMATCH[1]}"
        echo "** found cmake version ${regex_match}"
        if [ -d "/usr/share/cmake-${regex_match}" ]; then
            echo "** updating cmake platform in path /usr/share/cmake-${regex_match}"
            cp --verbose $SCRIPTPATH/*.cmake /usr/share/cmake-${regex_match}/Modules/Platform/
        elif [ -d "/usr/local/share/cmake-${regex_match}" ]; then
            echo "** updating cmake platform in path /usr/local/share/cmake-${regex_match}"
            cp --verbose $SCRIPTPATH/*.cmake /usr/local/share/cmake-${regex_match}/Modules/Platform/
        fi
else
    echo "** ERROR: unknown cmake version that regex did not match ${CMAKE_VERSION}"
fi
