cmake_minimum_required(VERSION 3.1)

project(VPU_MODEL VERSION 0.1 LANGUAGES CXX)
set (CMAKE_CXX_STANDARD 20)

add_executable(vpu
    src/main.cpp
    src/config.cpp
    src/manager_core.cpp
    src/memory.cpp
)
include_directories(include)
