#!/bin/bash

qemu-system-x86_64 -s -nodefaults -vga "std" -machine "q35,accel=kvm:tcg" -m "64M" \
    -drive format="raw,file=fat:rw:../VMTestBed/Boot/" \
    -drive if="pflash,format=raw,readonly,file=../Vendor/OVMF/OVMF_CODE.fd" \
    -drive if="pflash,format=raw,file=../Vendor/OVMF/OVMF_VARS.fd"