#!/bin/bash

if [ -e /usr/workspace/vali/vali-apps-nightly-"$VALI_ARCH"/vali-apps.tar.gz ]; then
  mv /usr/workspace/vali/vali-apps-nightly-"$VALI_ARCH"/vali-apps.tar.gz .
else
  mv /usr/workspace/vali/vali-apps-nightly-"$VALI_ARCH".zip .
  unzip vali-apps-nightly-"$VALI_ARCH".zip
fi
tar -xvf vali-apps.tar.gz
