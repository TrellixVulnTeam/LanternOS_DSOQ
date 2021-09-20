#!/usr/bin/env python3
import argparse
import subprocess
import os


def main():
    #script_path = os.path.split(os.path.realpath(__file__))
    # os.chdir(script_path[0])

    uefi_compiler_name = "$HOME/opt/LanternOS-toolchain/bin/x86_64-w64-mingw32-gcc"
    kernel_compiler_name = "$HOME/opt/LanternOS-toolchain/bin/x86_64-elf-gcc"
    build_type = "Release"
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--ueficompiler", help="Specify the full path to where you installed the uefi cross-compiler."
        " This is only needed if you installed the toolchain to a nondefault directory.")
    parser.add_argument(
        "--kernelcompiler", help="Specify the full path to where you installed the elf cross-compiler."
        " This is only needed if you installed the toolchain to a nondefault directory.")
    parser.add_argument("--build", help="Whether to build Debug or Release. Default is release.")
    args = parser.parse_args()

    if args.ueficompiler:
        uefi_compiler_name = args.ueficompiler
    if args.kernelcompiler:
        kernel_compiler_name = args.kernelcompiler
    if args.build:
        build_type = args.build

    uefi_compiler_name = os.path.expandvars(uefi_compiler_name)
    kernel_compiler_name = os.path.expandvars(kernel_compiler_name)

    subprocess.run(["cmake", "-S../bhavaloader/",
                    "-B../build/{}/bhavaloader".format(build_type),
                    "-DCMAKE_BUILD_TYPE={}".format(build_type), "-DCMAKE_C_COMPILER={}".format(uefi_compiler_name)])
    subprocess.run(["make", "-C../build/{}/bhavaloader".format(build_type)])

    # TODO: Build kernel

    os.makedirs("../VMTestBed/Boot/EFI/Boot/", exist_ok=True)
    os.replace("../build/{}/bhavaloader/BhavaLoader.exe".format(build_type),
               "../VMTestBed/Boot/EFI/Boot/Bootx64.efi")


if __name__ == "__main__":
    main()
