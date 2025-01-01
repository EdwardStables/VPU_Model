#pragma once
#ifdef RPC
#include "simulator_interface.h"
#include "defs_pkg.h"
#include "memory.h"
#include "debug.h"
#include "manager_core.h"

namespace vpu::rpc {

class ServerInterface : public SimulatorRPCInterface {
    vpu::mem::Memory* memory;
    vpu::ManagerCore* core;
    vpu::Debug& debug;
    std::optional<CommandType> last_command = std::nullopt;
    uint32_t pc = 0; //mirrors that of the main core
public:
    ServerInterface(vpu::mem::Memory* memory, vpu::Debug& debug, vpu::ManagerCore* core);
    std::array<uint8_t,512> get_memory_segment(uint32_t address);
    std::vector<std::string> get_source_code();

    void set_command(CommandType command);
    std::optional<CommandType> get_command();

    uint32_t get_pc();
    void set_pc(uint32_t pc);

};

}

#endif
