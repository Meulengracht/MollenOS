#!/bin/bash

if [ $# -ne 1 ]; then
    printf "Usage: %s <image_file>\n" "$(basename "$0")"
    exit 1
fi

IMAGE_FILE=$1
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# The image file is refered to by the setup.bochsrc
# Additionally, we have a usb image setup too for bochs
rm -f "${SCRIPT_DIR}/disk.img" "${SCRIPT_DIR}/disk_usb.img"
cp "${IMAGE_FILE}" "${SCRIPT_DIR}/disk.img"
cp "${IMAGE_FILE}" "${SCRIPT_DIR}/disk_usb.img"

# invoke bochs with the setup, but cd to the script directory
pushd "${SCRIPT_DIR}"
trap "popd" EXIT

bochs -q -f "${SCRIPT_DIR}/setup.bochsrc"
