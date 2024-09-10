cmake_minimum_required(VERSION 3.1)

project(VPU_MODEL VERSION 0.1 LANGUAGES CXX)
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)
set (CMAKE_CXX_STANDARD 20)

set (VPU_INSPECTOR ${CMAKE_CURRENT_SOURCE_DIR}/VPU_Inspector)
set (VPU_ASM ${CMAKE_CURRENT_SOURCE_DIR}/VPU_ASM)
set (VPU_DEFS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/defs)

### NON-CONFIGURABLE CONSTANTS IN CODE ###
set (VPU_DEFS_NAME defs_pkg)
set (DEFS_NAMESPACE vpu::defs)
### NON-CONFIGURABLE CONSTANTS IN CODE ###

add_custom_command(
    OUTPUT ${VPU_DEFS_DIR}/${VPU_DEFS_NAME}.h ${VPU_DEFS_DIR}/${VPU_DEFS_NAME}.cpp
    DEPENDS ${VPU_ASM}/instructions.yaml
            ${VPU_ASM}/instructions.py
            ${VPU_ASM}/templates/cpp_def.cpp.j2
            ${VPU_ASM}/templates/cpp_def.h.j2
    COMMAND ${VPU_ASM}/instructions.py ${VPU_ASM}/instructions.yaml --cpp ${VPU_DEFS_DIR}/${VPU_DEFS_NAME} --namespace ${DEFS_NAMESPACE}
)

option(NORPC "Disable compilation with RPC. Allows you to miss the inspector submodule and having a gRPC install")

add_executable(vpu
    src/main.cpp
    src/config.cpp
    src/manager_core.cpp
    src/memory.cpp
    src/dma.cpp
    src/scheduler.cpp
    src/blitter.cpp
    src/rpc_interface.cpp
    ${VPU_DEFS_DIR}/${VPU_DEFS_NAME}.cpp
)

#RPC is enabled, attempt to link with library in inspector submodule
if (NOT ${NORPC})
    add_subdirectory(${VPU_INSPECTOR} ${VPU_INSPECTOR}/build)
    target_link_libraries(vpu rpc_server)
    target_include_directories(vpu PRIVATE $VPU_INSPECTOR/simulator_include)
    add_compile_definitions(RPC)
endif()

target_include_directories(
    vpu
    PRIVATE include
    PRIVATE ${VPU_DEFS_DIR}
)


