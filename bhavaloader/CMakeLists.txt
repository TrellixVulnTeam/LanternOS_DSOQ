cmake_minimum_required(VERSION 3.16.0)

set(CMAKE_CXX_COMPILER_WORKS ON)
set(MINGW_HEADERS_DIR "" CACHE STRING "Provide the full path to the mingw headers.")
# This is mostly for testing in QEMU. The bootloader attempts to select the highest supported resolution,
# which for QEMU is 2560x1600. This causes part of the screen to be cut off do the small size of the 
# qemu window. If UEFI cant find the custom res you specified, it will print an error and exit.
set(CUSTOM_RESOLUTION ON)

if (CUSTOM_RESOLUTION)
   add_compile_definitions(CUSTOM_RESOLUTION)
   add_compile_definitions(CUSTOM_RESOLUTION_X=1280)
   add_compile_definitions(CUSTOM_RESOLUTION_Y=720)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../build/${CMAKE_BUILD_TYPE}/bhavaloader/bin")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
project(BhavaLoader CXX)

set(SOURCES "src/main.cpp")

add_executable(BhavaLoader  ${SOURCES})

target_compile_options(BhavaLoader PRIVATE -ffreestanding -fno-stack-protector -mno-red-zone -fshort-wchar -fno-exceptions -fno-rtti)
target_link_options(BhavaLoader PRIVATE -nostdlib -Wl,--subsystem,10 -Wl,--entry,efi_main)
target_include_directories(BhavaLoader PRIVATE  "${CMAKE_SOURCE_DIR}/../Vendor/gnuefi"
                                                ${MINGW_HEADERS_DIR})