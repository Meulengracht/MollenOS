#!/bin/sh

if [ ! -f $CROSS/bin/clang ]; then
  mkdir -p $CROSS
  wget -O $CROSS/toolchain.zip https://www.dropbox.com/s/j8afomxfhwjl7yp/mollenos-bin-$CURRENT_DIST.zip?dl=1
  unzip $CROSS/toolchain.zip -d $CROSS
  rm $CROSS/toolchain.zip
fi
