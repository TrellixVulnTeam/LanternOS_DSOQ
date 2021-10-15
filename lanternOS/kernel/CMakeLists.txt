cmake_minimum_required(VERSION 3.16.0)

set(CMAKE_CXX_COMPILER_WORKS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../build/${CMAKE_BUILD_TYPE}/kernel/bin")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
project(LanternOS CXX)

set(SOURCES "src/kmain.cpp")

add_executable(LanternOS  ${SOURCES})

target_compile_options(LanternOS PRIVATE -g -ffreestanding -fno-stack-protector -mno-red-zone -fno-exceptions -fno-rtti -Wno-pointer-arith)
target_link_options(LanternOS PRIVATE -g -nostdlib -Wl,--entry,kmain)