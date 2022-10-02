#!/bin/bash
SCRIPT=`realpath $0`
SCRIPTPATH=`dirname "$SCRIPT"`

# Dev-libraries
echo "** installing build dependencies"
apt-get update -yqq
apt-get -y -qq install git cmake gcc g++ zip unzip nasm make python3 curl

# Install dotnet core
if ! [ -x "$(command -v dotnet)" ]; then
  echo "** installing dotnet core"
  "$SCRIPTPATH"/dotnet-install.sh --channel 3.1
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
            cp --verbose "$SCRIPTPATH"/*.cmake /usr/share/cmake-"${regex_match}"/Modules/Platform/
        elif [ -d "/usr/local/share/cmake-${regex_match}" ]; then
            echo "** updating cmake platform in path /usr/local/share/cmake-${regex_match}"
            cp --verbose "$SCRIPTPATH"/*.cmake /usr/local/share/cmake-"${regex_match}"/Modules/Platform/
        fi
else
    echo "** ERROR: unknown cmake version that regex did not match ${CMAKE_VERSION}"
fi
