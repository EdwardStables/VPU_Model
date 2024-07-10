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
    flags.fill(0);
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
        case vpu::defs::MOV_R_I16:
            registers[vpu::defs::get_register(instr,0)] = vpu::defs::get_u16(instr);
            break;
        case vpu::defs::ADD_I24:
            registers[vpu::defs::ACC] += vpu::defs::get_u24(instr);
            break;
        case vpu::defs::JMP_L:
            next_pc = vpu::defs::get_label(instr);
            break;
        case vpu::defs::CMP_R:
            flags[0] = registers[vpu::defs::get_register(instr,0)] == 0;
            break;
        case vpu::defs::CMP_R_R:
            std::cout << "flag set " << flags[0];
            flags[0] = registers[vpu::defs::get_register(instr,0)] == registers[vpu::defs::get_register(instr,1)];
            std::cout << " " << flags[0] << std::endl;
            break;
        case vpu::defs::BRA_L:
            if (flags[0]) next_pc = vpu::defs::get_label(instr);
            break;
        default:
            std::cerr << "Error decoding opcode " << vpu::defs::opcode_to_string(opcode);
            std::cerr << " at address " << std::hex << PC() << std::endl;
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
    for (int i = 0; i < vpu::defs::REGISTER_COUNT; i++){
        std::cout << vpu::defs::register_to_string((vpu::defs::Register)i);
        std::cout << " " << std::hex << registers[i] << " \t";
    }
    std::cout << "  C " << flags[0] << "  Z " << flags[1];
    std::cout << "\n";
}

}
