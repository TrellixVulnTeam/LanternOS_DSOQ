cmake_minimum_required(VERSION 3.16.0)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
project(BhavaLoader CXX)

set(SOURCES "src/efi_main.cpp"
            "${CMAKE_SOURCE_DIR}/../Vendor/gnu-efi/lib/data.c")

add_executable(BhavaLoader  ${SOURCES})
target_compile_options(BhavaLoader PRIVATE -ffreestanding -fno-stack-protector -mno-red-zone -fshort-wchar -fno-exceptions -fno-rtti)
target_link_options(BhavaLoader PRIVATE -nostdlib -Wl,--subsystem,10 -Wl,--entry,efi_main)
target_include_directories(BhavaLoader PRIVATE "${CMAKE_SOURCE_DIR}/../Vendor/gnu-efi/inc"
                                              "${CMAKE_SOURCE_DIR}/../Vendor/gnu-efi/inc/x86_64"
                                              "${CMAKE_SOURCE_DIR}/../Vendor/gnu-efi/inc/protocol"
                                              "${CMAKE_SOURCE_DIR}/../Vendor/gnu-efi/lib/")