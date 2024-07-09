#include <iostream>
#include <assert.h>

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
    uint32_t instr = memory->read(PC());
    uint32_t next_pc = PC() + 4;

    if (instr == vpu::defs::SEGMENT_END){
        has_halted = true;
        return;
    }

    vpu::defs::Opcode opcode = vpu::defs::get_opcode(instr);

    switch(opcode) {
        case vpu::defs::NOP:
            break;
        case vpu::defs::HLT:
            next_pc = PC();
            has_halted = true;
            break;
        case vpu::defs::MOV_I24:
            registers[vpu::defs::ACC] = vpu::defs::get_u24(instr);
            break;
        case vpu::defs::ADD_I24:
            registers[vpu::defs::ACC] += vpu::defs::get_u24(instr);
            break;
        case vpu::defs::JMP_L:
            next_pc = vpu::defs::get_label(instr);
            break;
        default:
            assert(false);
    }


    update_pc(next_pc);
}

bool ManagerCore::check_has_halted() {
    return has_halted;
}

uint32_t ManagerCore::PC() {
    return registers[vpu::defs::PC];
}

void ManagerCore::print_trace(uint32_t cycle) {
    std::cout << "Cycle: " << cycle << "\t";
    std::cout << "PC: " << std::hex << PC() << "\t";
    std::cout << "ACC: " << registers[vpu::defs::ACC] << "\t";
    std::cout << "\n";
}

}
