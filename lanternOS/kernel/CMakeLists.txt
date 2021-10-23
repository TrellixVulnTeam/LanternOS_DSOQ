cmake_minimum_required(VERSION 3.16.0)

set(CMAKE_CXX_COMPILER_WORKS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../../build/${CMAKE_BUILD_TYPE}/kernel/bin")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
project(LanternOS CXX)

set(SOURCES "src/kmain.cpp"
            "src/tty/tty.cpp")

add_executable(LanternOS  ${SOURCES})

target_compile_options(LanternOS PRIVATE -g -Wall -Wextra -ffreestanding -fno-stack-protector -mno-red-zone -fno-exceptions -fno-rtti -Wno-pointer-arith -fPIC -fno-use-cxa-atexit)
target_link_libraries(LanternOS "${CMAKE_CURRENT_SOURCE_DIR}/../../namelesslibc/build/${CMAKE_BUILD_TYPE}/namelesslibc/bin/libnamelesslibk.a")
target_link_options(LanternOS PRIVATE -g -nostdlib -Wl,--entry,kmain -fPIC)
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../namelesslibc/libk/include/"
                     "${CMAKE_CURRENT_SOURCE_DIR}/include/")