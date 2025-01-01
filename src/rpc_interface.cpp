#include <memory.h>
#ifdef RPC
#include "rpc_interface.h"
#include <iostream>
#include <assert.h>

namespace vpu::rpc {

ServerInterface::ServerInterface(vpu::mem::Memory* memory, vpu::Debug& debug, vpu::ManagerCore* core)
    : memory(memory), debug(debug), core(core)
{
}

std::array<uint8_t,512> ServerInterface::get_memory_segment(uint32_t addr) {
    assert((addr & 0xFF) == 0);
    std::array<uint8_t,512> ret;
    auto& data = mem::MemorySnooper::get_data(memory);
    std::copy(data.begin()+addr,data.begin()+addr+512, ret.begin());
    return ret;
}

std::vector<std::string> ServerInterface::get_source_code() {
    if (!debug.valid) return {};
    return debug.get_source_code();
}

std::optional<CommandType> ServerInterface::get_command() {
    auto to_ret = last_command;
    last_command = std::nullopt; //TODO: not thread safe, but unlikely to cause an issue right now. Revisit later
    return to_ret;
}

void ServerInterface::set_command(CommandType command) {
    last_command = command;
}

void ServerInterface::set_pc(uint32_t pc) {
    this->pc = pc;
}

uint32_t ServerInterface::get_pc() {
    return pc;
}

}

#endif
