cmake_minimum_required(VERSION 3.13)

project(VPU_MODEL VERSION 0.1 LANGUAGES CXX)
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)
set (CMAKE_CXX_STANDARD 20)

set (VPU_INSPECTOR ${CMAKE_CURRENT_SOURCE_DIR}/VPU_Inspector)
set (VPU_ASM ${CMAKE_CURRENT_SOURCE_DIR}/VPU_ASM)
set (VPU_DEFS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/defs)

option(NORPC "Disable compilation with RPC. Allows you to miss the inspector submodule and having a gRPC install")

### NON-CONFIGURABLE CONSTANTS IN CODE ###
set (VPU_DEFS_NAME defs_pkg)
set (DEFS_NAMESPACE vpu::defs)
### NON-CONFIGURABLE CONSTANTS IN CODE ###
add_subdirectory(${VPU_ASM})

add_executable(vpu
    src/main.cpp
    src/config.cpp
    src/manager_core.cpp
    src/memory.cpp
    src/dma.cpp
    src/scheduler.cpp
    src/blitter.cpp
    src/rpc_interface.cpp
    src/debug.cpp
    ${VPU_DEFS_DIR}/${VPU_DEFS_NAME}.cpp
)

#Runs python generator scripts
target_link_libraries(vpu vpu_defs)

target_compile_options(vpu PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)
target_link_options(vpu PRIVATE $<$<CONFIG:Debug>:-fsanitize=address>)

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


#Needed for taking MD5 hash
#TODO: Roll own MD5 to avoid dependency
find_package(OpenSSL REQUIRED)
message("OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
message("OpenSSL libraries: ${OPENSSL_LIBRARIES}")
include_directories(${OPENSSL_INCLUDE_DIR})
list(APPEND LIB_LIST ${OPENSSL_LIBRARIES})
