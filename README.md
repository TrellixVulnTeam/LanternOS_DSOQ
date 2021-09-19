# LanternOS




# Building

Currently you can only build on linux.

You will need:

* Python 3.6 or higher
* CMake 3.16.0 or higher
* The build depencies required for compiling gcc: https://wiki.osdev.org/GCC_Cross-Compiler#Installing_Dependencies
* Qemu is used by default for running in a VM.

1. Run scripts/install-toolchain.py. By default it will install into $HOME/opt/LanternOS-toolchain.
You can specify a different install directory with --installpath.
2. Run build.py. By default it will look for the cross-compilers in $HOME/opt/LanternOS-toolchain.
If you specified a custom install directory, you will need to provide the full path to them to the script.