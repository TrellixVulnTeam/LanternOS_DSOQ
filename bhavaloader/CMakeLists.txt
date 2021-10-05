cmake_minimum_required(VERSION 3.16.0)

set(CMAKE_CXX_COMPILER_WORKS ON)
set(MINGW_HEADERS_DIR "" CACHE STRING "Provide the full path to the mingw headers.")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../build/${CMAKE_BUILD_TYPE}/bhavaloader/bin")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
project(BhavaLoader CXX)

set(SOURCES "src/main.cpp")

add_executable(BhavaLoader  ${SOURCES})

target_compile_options(BhavaLoader PRIVATE -ffreestanding -fno-stack-protector -mno-red-zone -fshort-wchar -fno-exceptions -fno-rtti)
target_link_options(BhavaLoader PRIVATE -nostdlib -Wl,--subsystem,10 -Wl,--entry,efi_main)
target_include_directories(BhavaLoader PRIVATE "${CMAKE_SOURCE_DIR}/efi/"
                                                ${MINGW_HEADERS_DIR})