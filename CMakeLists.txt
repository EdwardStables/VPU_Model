cmake_minimum_required(VERSION 3.1)

project(VPU_MODEL VERSION 0.1 LANGUAGES CXX)
set (CMAKE_CXX_STANDARD 20)

set (VPU_ASM ${CMAKE_CURRENT_SOURCE_DIR}/VPU_ASM)
set (VPU_DEFS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/defs)
set (VPU_DEFS_NAME defs_pkg)

add_custom_command(
    OUTPUT ${VPU_DEFS_DIR}/${VPU_DEFS_NAME}.h ${VPU_DEFS_DIR}/${VPU_DEFS_NAME}.cpp
    DEPENDS ${VPU_ASM}/instructions.yaml ${VPU_ASM}/instructions.py
    COMMAND ${VPU_ASM}/instructions.py ${VPU_ASM}/instructions.yaml --cpp ${VPU_DEFS_DIR}/${VPU_DEFS_NAME}
)

add_executable(vpu
    src/main.cpp
    src/config.cpp
    src/manager_core.cpp
    src/memory.cpp
    ${VPU_DEFS_DIR}/${VPU_DEFS_NAME}.cpp
)
target_include_directories(
    vpu
    PRIVATE include
    PRIVATE ${VPU_DEFS_DIR}
)