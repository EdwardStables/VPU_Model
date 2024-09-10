#pragma once
#ifdef RPC
#include "simulator_interface.h"
#include "defs_pkg.h"
#include "memory.h"

namespace vpu::rpc {

class ServerInterface : public SimulatorRPCInterface {
    std::unique_ptr<vpu::mem::Memory>& memory;
public:
    ServerInterface(std::unique_ptr<vpu::mem::Memory>& memory);
    virtual std::array<uint8_t,512> get_memory_segment(uint32_t address) override;
};

}

#endif
