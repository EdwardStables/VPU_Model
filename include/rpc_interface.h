#pragma once
#ifdef RPC
#include "simulator_interface.h"
#include "defs_pkg.h"
#include "memory.h"
#include "debug.h"

namespace vpu::rpc {

class ServerInterface : public SimulatorRPCInterface {
    vpu::mem::Memory* memory;
    vpu::Debug& debug;
public:
    ServerInterface(vpu::mem::Memory* memory, vpu::Debug& debug);
    std::array<uint8_t,512> get_memory_segment(uint32_t address);
    std::vector<std::string> get_source_code();
};

}

#endif
