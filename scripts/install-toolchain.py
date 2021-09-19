#!/usr/bin/env python3

import argparse
import os
import sys
import urllib.request as request_file
import tarfile
import subprocess

# TODO: Consider a way to automatically grab latest version?
GCC_SOURCE_URL = "https://ftpmirror.gnu.org/gcc/gcc-11.2.0/gcc-11.2.0.tar.gz"
BINUTILS_SOURCE_URL = "https://ftpmirror.gnu.org/binutils/binutils-2.37.tar.xz"


def fetch_source(tool_name, source_url, download_dir):
    print("Fetching {} source files...".format(tool_name), end="", flush=True)
    old_dir = os.getcwd()
    os.makedirs(download_dir, exist_ok=True)
    os.chdir(download_dir)

    try:
        filename = "{}-source.tar.gz".format(tool_name)
        # Don't redownload if we already have copies.
        if (not os.path.exists(os.getcwd() + "/" + filename)):
            request_file.urlretrieve(source_url, filename)
    except:
        print("Failed!")
        print("Could not download {} sources to {}.".format(tool_name, os.getcwd()))
        sys.exit()

    try:
        with tarfile.open(filename) as gcc_source_tar:
            remove_root = gcc_source_tar.getmembers()
            remove_root = remove_root[1:]
            for member in remove_root:
                member.path = "src/" + member.path[member.path.index("/")+1:]

            gcc_source_tar.extractall(members=remove_root)
    except:
        print("Failed!")
        print("Could not extract file {}".format(tool_name))
        sys.exit()

    print("Success!")
    os.chdir(old_dir)


def configure_source(tool_name, build_target, install_loc, args):
    print("Preparing {} of target {} for compilation...".format(tool_name, build_target), end="", flush=True)
    old_dir = os.getcwd()
    os.chdir(install_loc)

    try:
        os.makedirs("build/{}-{}".format(tool_name, build_target))
    except FileExistsError as e:
        os.chdir("build/{}-{}".format(tool_name, build_target))
        subprocess.run(["make", "distclean"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.chdir("../../")

    os.chdir("build/{}-{}".format(tool_name, build_target))

    try:
        configure_file_dir = ""
        if tool_name == "binutils":
            configure_file_dir = "../../download/binutils-source/"
        elif tool_name == "gcc":
            configure_file_dir = "../../download/gcc-source/"

        subprocess.run([configure_file_dir + "src/configure", "--target={}".format(build_target),
                        "--prefix={}".format(install_loc), *args],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as e:
        print("Failed!")
        print("Could not configure {}".format(tool_name))
        sys.exit()

    build_dir = os.getcwd()

    print("Success!")
    os.chdir(old_dir)
    return build_dir


def build_binutils(target, build_dir, job_number):
    print("Building binutils of target {}...".format(target), end="", flush=True)
    old_dir = os.getcwd()
    os.chdir(build_dir)

    try:
        result = subprocess.run(["make", "-j{}".format(job_number)],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        result = subprocess.run(["make", "install"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except subprocess.CalledProcessError as e:
        print("Failed!")
        print("Failed to build binutils...")
        sys.exit()

    print("Success!")
    os.chdir(old_dir)


# TODO: Modify this to account for both the bootloader and the kernel gcc cross-compilers.
def prepare_modified_libgcc(gcc_path):
    print("Modifying gcc config to build libgcc with no redzone...", end="", flush=True)
    old_dir = os.getcwd()
    os.chdir(gcc_path)
    os.chdir("src/gcc/config/i386")

    if (not os.path.exists("nrz-x86_64-mingw")):
        with open("nrz-x86_64-mingw", 'w') as file:
            file.write("MULTILIB_OPTIONS += mno-red-zone\n")
            file.write("MULTILIB_DIRNAMES += no-red-zone\n")

    if (not os.path.exists("nrz-x86_64-elf")):
        with open("nrz-x86_64-elf", 'w') as file:
            file.write("MULTILIB_OPTIONS += mno-red-zone\n")
            file.write("MULTILIB_DIRNAMES += no-red-zone\n")

    file_lines = []
    with open("../../config.gcc", 'r') as file:
        file_lines = file.readlines()
        file_iter = iter(file_lines)
        for line in file_iter:
            if "x86_64-*-elf*" in line:
                if next(file_iter) != "tmake_file=\"${tmake_file} i386/nrz-x86_64-elf\"":
                    line_index = file_lines.index(line)
                    file_lines.insert(line_index+1, "tmake_file=\"${tmake_file} i386/nrz-x86_64-elf\"\n")

            if "x86_64-*-mingw*" in line:
                if next(file_iter) != "tmake_file=\"${tmake_file} i386/nrz-x86_64-mingw\"":
                    line_index = file_lines.index(line)
                    file_lines.insert(line_index+1, "tmake_file=\"${tmake_file} i386/nrz-x86_64-mingw\"\n")

    with open("../../config.gcc", 'w') as file:
        file.writelines(file_lines)

    print("Success!")
    os.chdir(old_dir)


def build_gcc(target, build_dir, job_number):
    print("Building gcc of target {}...".format(target), end="", flush=True)
    old_dir = os.getcwd()
    os.chdir(build_dir)

    try:
        subprocess.run(["make", "all-gcc", "-j{}".format(job_number)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["make", "all-target-libgcc", "-j{}".format(job_number)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["make", "install-gcc"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["make", "install-target-libgcc"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except:
        print("Failed!")
        print("Failed to build gcc...")
        sys.exit()

    print("Success!")
    os.chdir(old_dir)


def main():
    command_parser = argparse.ArgumentParser(description="This script downloads and builds cross-compilers"
                                             " for the bootloader and the kernel. The default installation"
                                             " directory for the toolchain is $HOME/opt/LanternOS-toolchain."
                                             " The bin directory containing the executables for the"
                                             " compilers will be automatically added to your $PATH variable"
                                             " if possible.")
    command_parser.add_argument("--installpath", help="Specify a custom installpath for the toolchain."
                                " The script will still automatically attempt to add the bin/ directory"
                                " to your $PATH.")
    command_parser.add_argument("--jobnumber", help="Specify a number of jobs for the -j argument.")

    user_args = command_parser.parse_args()
    install_location = os.path.expandvars("$HOME/opt/LanternOS-toolchain/")
    job_number = 1
    if user_args.installpath:
        install_location = os.path.expandvars(user_args.installpath)
        if not os.path.isdir(install_location):
            print("Error: {} does not appear to be a directory.".format(install_location))
            sys.exit()
    if user_args.jobnumber:
        try:
            job_number = (int)(user_args.jobnumber)
        except ValueError:
            print("Error: Did not understand jobnumber argument.")
            sys.exit()

    os.makedirs(install_location, exist_ok=True)
    os.environ["PATH"] += ":" + install_location + "/bin"

    print("Downloading toolchain source...")
    cached_script_path = os.getcwd()
    try:
        os.chdir(install_location)
    except PermissionError:
        print("Did not have sufficient permissions to chdir into {}".format(install_location))
        sys.exit()

    fetch_source("gcc", GCC_SOURCE_URL, "download/gcc-source")
    fetch_source("binutils", BINUTILS_SOURCE_URL, "download/binutils-source")

    # chdir back to root install dir to be safe
    os.chdir(install_location)
    binutils_build_dir = configure_source("binutils", "x86_64-w64-mingw32", install_location, ["--with-sysroot",
                                                                                               "--disable-nls", "--disable-werror"])
    build_binutils("x86_64-w64-mingw32", binutils_build_dir, job_number)

    prepare_modified_libgcc("download/gcc-source")
    gcc_build_dir = configure_source("gcc", "x86_64-w64-mingw32", install_location,
                                     ["--disable-nls", "--enable-languages=c,c++", "--without-headers"])
    build_gcc("x86_64-w64-mingw32", gcc_build_dir, job_number)

    # TODO: build cross-compiler for kernel.


if __name__ == "__main__":
    main()
