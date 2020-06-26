#!/bin/bash
SCRIPT=`realpath $0`
SCRIPTPATH=`dirname $SCRIPT`

# Dev-libraries
apt-get update
apt-get -y -qq install git cmake gcc g++ zip nasm mono-complete make python

# Install the cmake platform template
CMAKE_VERSION="$(cmake --version)";
CMAKE_REGEX="cmake version ([0-9]+\.[0-9]+)\.[0-9]+"
if [[ $CMAKE_VERSION =~ $CMAKE_REGEX ]]
    then
        regex_match="${BASH_REMATCH[1]}"
        echo "Found cmake version ${regex_match}"
        if [ -d "/usr/share/cmake-${regex_match}" ]; then
            echo "Updating cmake platform in path /usr/share/cmake-${regex_match}"
            sudo cp --verbose $SCRIPTPATH/*.cmake /usr/share/cmake-${regex_match}/Modules/Platform/
        elif [ -d "/usr/local/share/cmake-${regex_match}" ]; then
            echo "Updating cmake platform in path /usr/local/share/cmake-${regex_match}"
            sudo cp --verbose $SCRIPTPATH/*.cmake /usr/local/share/cmake-${regex_match}/Modules/Platform/
        fi
else
    echo "Unknown cmake version that regex did not match ${CMAKE_VERSION}"
fi
