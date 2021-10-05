#!/bin/bash

qemu-system-x86_64 -s -nodefaults -vga "std" -machine "q35,accel=kvm:tcg" -m "64M" \
    -drive format="raw,file=fat:rw:../VMTestBed/Boot/" \
    -drive if="pflash,format=raw,readonly=on,file=../Vendor/OVMF/OVMF_CODE.fd"