#!/bin/bash

if [ -e /usr/workspace/vali/vali-apps-nightly-"$VALI_ARCH"/vali-apps.tar.gz ]; then
  mv /usr/workspace/vali/vali-apps-nightly-"$VALI_ARCH"/vali-apps.tar.gz .
  tar -xvf vali-apps.tar.gz
elif [ -e /usr/workspace/vali/vali-apps-nightly-"$VALI_ARCH".zip ]; then
  mv /usr/workspace/vali/vali-apps-nightly-"$VALI_ARCH".zip .
  unzip vali-apps-nightly-"$VALI_ARCH".zip
  tar -xvf vali-apps.tar.gz
fi

