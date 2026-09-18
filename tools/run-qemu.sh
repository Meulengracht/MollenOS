#!/bin/bash

if [ $# -ne 1 ]; then
    printf "Usage: %s <image_file>\n" "$(basename "$0")"
    exit 1
fi

IMAGE_FILE=$1
OVMF_CODE="/usr/share/OVMF/x64/OVMF_CODE.4m.fd"
OVMF_VARS_ORIG="/usr/share/OVMF/x64/OVMF_VARS.4m.fd"

OVMF_VARS="$(basename "${OVMF_VARS_ORIG}")"
if [ ! -e "${OVMF_VARS}" ]; then
    if ! cp "${OVMF_VARS_ORIG}" "${OVMF_VARS}"; then
        printf "Unable to create OVMF variables file: %s\n" "${OVMF_VARS}" >&2
        exit 1
    fi
fi

if [ ! -w "${OVMF_VARS}" ]; then
    printf "OVMF variables file is not writable: %s\n" "${OVMF_VARS}" >&2
    exit 1
fi

qemu-system-x86_64 \
    -D ./log.txt -monitor stdio -smp 1 -m 4096 \
    -net none -no-reboot \
    -drive if=pflash,format=raw,readonly=on,file="${OVMF_CODE}" \
    -drive if=pflash,format=raw,file="${OVMF_VARS}" \
    -drive if=virtio,file="${IMAGE_FILE}",format=raw \
    -serial file:./qserial.txt
