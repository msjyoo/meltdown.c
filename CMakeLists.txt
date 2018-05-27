cmake_minimum_required(VERSION 3.10)
project(meltdown C)

set(CMAKE_C_STANDARD 99)

add_executable(meltdown meltdown.c)
target_compile_options(meltdown PRIVATE -masm=intel)