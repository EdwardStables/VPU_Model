#include <iostream>

#include "manager_core.h"

namespace vpu::core {

ManagerCore::ManagerCore(
    vpu::config::Config& config,
    std::unique_ptr<vpu::mem::Memory>& memory
) :
    config(config),
    memory(memory),
    has_halted(false)
{
    registers.fill(0);
}

void ManagerCore::update_pc(uint32_t new_pc) {
    registers[vpu::defs::PC] = new_pc;
}

void ManagerCore::run_cycle() {
    uint32_t next_instr = memory->read(PC());
    uint32_t next_pc = PC() + 4;

    if (next_instr == vpu::defs::SEGMENT_END){
        has_halted = true;
        return;
    }

    vpu::defs::Opcode opcode = vpu::defs::get_opcode(next_instr);
    std::cout << vpu::defs::opcode_to_string(opcode) << std::endl;

    update_pc(next_pc);
}

bool ManagerCore::check_has_halted() {
    return has_halted;
}

uint32_t ManagerCore::PC() {
    return registers[vpu::defs::PC];
}

}