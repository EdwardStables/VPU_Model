#include <iostream>
#include <assert.h>
#include <optional>

#include "manager_core.h"

namespace vpu::core {

uint32_t ManagerCoreSnooper::get_register(ManagerCore& core, vpu::defs::Register reg) {
    return core.registers[reg];
}

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
    //Run pipeline backwards to avoid losing data
    //Each one returns data to be in in the following cycle

    stage_writeback();
    stage_memory();
    stage_execute();
    //actual register stage writeback must be done after execution and memory stages complete
    stage_writeback_commit();
    stage_decode();
    stage_fetch();
}

void ManagerCore::stage_fetch() {
    if (!flush_set && fetch_seen_hlt){
        fetch_valid_output = false;
        return;
    }
    fetch_seen_hlt = false;

    //TODO: icache and stalling
    fetch_valid_output = true;
    decode_instruction = memory->read(PC());
    
    //Don't increment PC or end output for segment end.
    if (vpu::defs::get_opcode(decode_instruction) == vpu::defs::HLT){
        fetch_seen_hlt = true;
        return;
    }

    uint32_t next_pc = flush_set ? flush_next_pc : PC() + 4;
    update_pc(next_pc);

    //Halt after fetch to retain correct final PC on HLT flush
    if (has_halted){
        fetch_valid_output = false;
        return;
    }
    decode_next_pc = next_pc;
}

void ManagerCore::stage_decode() {
    if (!fetch_valid_output || flush_set){
        decode_valid_output = false;
        return;
    }

    if (decode_instruction == vpu::defs::SEGMENT_END){
        has_halted = true;
        decode_valid_output = false;
        return;
    }

    //If above not triggered then the output must be valid
    decode_valid_output = true;
    execute_next_pc = decode_next_pc;

    execute_opcode = vpu::defs::get_opcode(decode_instruction);
    uint8_t reg_index;

    execute_dest = (vpu::defs::Register)0;
    execute_source0 = 0;
    execute_source1 = 0;

    //dest
    switch(execute_opcode) {
        //Nothing
        case vpu::defs::NOP:
        case vpu::defs::HLT:
        case vpu::defs::JMP_L:
        case vpu::defs::BRA_L:
        case vpu::defs::CMP_R:
        case vpu::defs::CMP_R_R:
            break;
        //Immediate 24-bit
        case vpu::defs::MOV_I24:
        case vpu::defs::ADD_I24:
        case vpu::defs::ASR_I24:
        case vpu::defs::LSR_I24:
        case vpu::defs::LSL_I24:
        case vpu::defs::ASR_R:
        case vpu::defs::LSR_R:
        case vpu::defs::LSL_R:
            execute_dest = vpu::defs::ACC;
            break;
        //Register destination
        case vpu::defs::MOV_R_I16:
        case vpu::defs::MOV_R_R:
            execute_dest = vpu::defs::get_register(decode_instruction,0);
            break;
        default:
            std::cerr << "Error decoding opcode " << vpu::defs::opcode_to_string(execute_opcode);
            std::cerr << " at address " << std::hex << PC();
            std::cerr << " for dest operand" << std::endl;
            assert(false);
    }
    //source0
    switch(execute_opcode) {
        //Nothing
        case vpu::defs::NOP:
        case vpu::defs::HLT:
            break;
        //Immediate 24-bit
        case vpu::defs::MOV_I24:
        case vpu::defs::ADD_I24:
        case vpu::defs::ASR_I24:
        case vpu::defs::LSR_I24:
        case vpu::defs::LSL_I24:
            execute_source0 = vpu::defs::get_u24(decode_instruction);
            break;
        //Register destination
        case vpu::defs::MOV_R_I16:
            execute_source0 = vpu::defs::get_u16(decode_instruction);
            break;
        case vpu::defs::CMP_R:
        case vpu::defs::ASR_R:
        case vpu::defs::LSR_R:
        case vpu::defs::LSL_R:
            reg_index = vpu::defs::get_register(decode_instruction,0);
            execute_source0 = execute_feedback_reg_held[reg_index] ?
                                    execute_feedback_reg_value[reg_index] :
                                    registers[reg_index];
            break;
        case vpu::defs::CMP_R_R:
        case vpu::defs::MOV_R_R:
            reg_index = vpu::defs::get_register(decode_instruction,1);
            execute_source0 = execute_feedback_reg_held[reg_index] ?
                                    execute_feedback_reg_value[reg_index] :
                                    registers[reg_index];
            break;
        //Label
        case vpu::defs::JMP_L:
        case vpu::defs::BRA_L:
            execute_source0 = vpu::defs::get_label(decode_instruction);
            break;
        default:
            std::cerr << "Error decoding opcode " << vpu::defs::opcode_to_string(execute_opcode);
            std::cerr << " at address " << std::hex << PC();
            std::cerr << " source operand" << std::endl;
            assert(false);
    }
    //source1
    switch(execute_opcode) {
        //Nothing
        case vpu::defs::NOP:
        case vpu::defs::HLT:
        case vpu::defs::MOV_I24:
        case vpu::defs::MOV_R_I16:
        case vpu::defs::MOV_R_R:
        case vpu::defs::JMP_L:
        case vpu::defs::BRA_L:
            break;
        //applied to ACC
        case vpu::defs::ADD_I24:
        case vpu::defs::ASR_I24:
        case vpu::defs::LSR_I24:
        case vpu::defs::LSL_I24:
        case vpu::defs::ASR_R:
        case vpu::defs::LSR_R:
        case vpu::defs::LSL_R:
            reg_index = vpu::defs::ACC;
            execute_source1 = execute_feedback_reg_held[reg_index] ?
                                    execute_feedback_reg_value[reg_index] :
                                    registers[reg_index];
            break;
        case vpu::defs::CMP_R_R:
            reg_index = vpu::defs::get_register(decode_instruction,0);
            execute_source1 = execute_feedback_reg_held[reg_index] ?
                                    execute_feedback_reg_value[reg_index] :
                                    registers[reg_index];
            break;
        case vpu::defs::CMP_R:
            execute_source1 = 0;
        default:
            std::cerr << "Error decoding opcode " << vpu::defs::opcode_to_string(execute_opcode);
            std::cerr << " at address " << std::hex << PC();
            std::cerr << " source operand" << std::endl;
            assert(false);
    }
}

void ManagerCore::stage_execute() {
    //Always reset flush, even if the input isn't valid, other stages are required to obey it
    flush_set = false;

    if (!decode_valid_output) {
        execute_valid_output = false;
        return;
    }

    memory_reg_index = (vpu::defs::Register)0; //indicated PC, which is invalid and will be ignored
    memory_reg_value = 0;
    memory_opcode = execute_opcode;
    execute_valid_output = true;
    
    bool check_flush = false;
    uint32_t next_pc = execute_next_pc;

    uint32_t unsigned_temp;
    int32_t signed_temp;
    switch(execute_opcode) {
        case vpu::defs::NOP:
        case vpu::defs::HLT: //HLT is not actually applied until the final stage to ensure full writeback completes
            break;
        case vpu::defs::MOV_R_I16:
        case vpu::defs::MOV_R_R:
        case vpu::defs::MOV_I24:
            memory_reg_index = execute_dest;
            memory_reg_value = execute_source0;
            break;
        case vpu::defs::ADD_I24:
            memory_reg_index = execute_dest;
            memory_reg_value = execute_source0 + execute_source1;
            break;
        case vpu::defs::CMP_R:
        case vpu::defs::CMP_R_R:
            //TODO does this need pipelining and going via writeback?
            if (execute_source0 == execute_source1)
                set_flag(vpu::defs::C);
            else
                unset_flag(vpu::defs::C);
            break;
        case vpu::defs::ASR_I24:
        case vpu::defs::ASR_R:
            signed_temp = (int32_t)execute_source1;
            signed_temp >>= execute_source0;
            memory_reg_index = execute_dest;
            memory_reg_value = signed_temp;
            break;
        case vpu::defs::LSR_I24:
        case vpu::defs::LSR_R:
            unsigned_temp = (int32_t)execute_source1;
            unsigned_temp >>= execute_source0;
            memory_reg_index = execute_dest;
            memory_reg_value = unsigned_temp;
            break;
        case vpu::defs::LSL_R:
        case vpu::defs::LSL_I24:
            unsigned_temp = (int32_t)execute_source1;
            unsigned_temp <<= execute_source0;
            memory_reg_index = execute_dest;
            memory_reg_value = unsigned_temp;
            break;
        case vpu::defs::BRA_L:
            check_flush = true;
            if (get_flag(vpu::defs::C))
                next_pc = execute_source0;
            break;
        case vpu::defs::JMP_L:
            check_flush = true;
            next_pc = execute_source0;
            break;
        default:
            std::cerr << "Error on opcode " << vpu::defs::opcode_to_string(execute_opcode);
            std::cerr << " at address " << std::hex << PC();
            std::cerr << " during execution" << std::endl;
            assert(false);
    }

    if (memory_reg_index != (vpu::defs::Register)0){
        execute_feedback_reg_held[(size_t)memory_reg_index] = true;
        execute_feedback_reg_value[(size_t)memory_reg_index] = memory_reg_value;
    }

    if (check_flush && execute_next_pc != next_pc){
        flush_set = true;
        flush_next_pc = next_pc;
    }
}

void ManagerCore::stage_memory() {
    //TODO: Implement memory accessing
    memory_valid_output = execute_valid_output;
    writeback_reg_index = memory_reg_index;
    writeback_reg_value = memory_reg_value;
    writeback_opcode = memory_opcode;
}

void ManagerCore::stage_writeback() {
    writeback_valid_output = memory_valid_output;
    writeback_commit_reg_index = writeback_reg_index;
    writeback_commit_reg_value = writeback_reg_value;

    if (writeback_opcode == vpu::defs::HLT)    
        has_halted = true;

    done_opcode = writeback_opcode;
}

void ManagerCore::stage_writeback_commit() {
    if (!writeback_valid_output || writeback_commit_reg_index == 0) return;

    registers[writeback_commit_reg_index] = writeback_commit_reg_value;
    if (execute_feedback_reg_value[writeback_commit_reg_index] == writeback_commit_reg_value) {
        execute_feedback_reg_held[writeback_commit_reg_index] = false;
    }
}

void ManagerCore::set_flag(vpu::defs::Flag flag) {
    flags[flag] = 1;
}

void ManagerCore::unset_flag(vpu::defs::Flag flag) {
    flags[flag] = 0;
}

bool ManagerCore::get_flag(vpu::defs::Flag flag) {
    return flags[flag];
}

bool ManagerCore::check_has_halted() {
    return has_halted;
}

uint32_t ManagerCore::PC() {
    return registers[vpu::defs::PC];
}

void ManagerCore::print_status_start() {
    if (!(config.pipeline || config.trace)) return;

    std::cout << "           ";
    if (config.pipeline)
        std::cout << "\t" << pipeline_heading();
    if (config.trace)
        std::cout << "\t" << trace_string();
    std::cout << "\n";
}

void ManagerCore::print_status(uint32_t cycle) {
    std::string cycle_str = "Cycle: " + std::to_string(cycle) + "  ";
    if (config.pipeline || config.trace)
        std::cout << cycle_str;

    if (config.pipeline)
        std::cout << "\t" << pipeline_string();

    if (config.trace)
        std::cout << "\t" << trace_string();

    std::cout << "\n";
}

std::string ManagerCore::pipeline_heading() {
    std::array<std::string, 5> headers = {"FETCH","DECODE","EXECUTE","MEMORY","WRITEBACK"};
    
    std::string op = "|";
    for (auto& h : headers){
        op += " ";
        h.insert(0, vpu::defs::MAX_OPCODE_LEN - h.size(), ' ');
        op += h;
        op += " |";
    }

    return op;
}

std::string ManagerCore::pipeline_string() {
    std::string op;    
    std::string na(vpu::defs::MAX_OPCODE_LEN, '-');

    op += "| ";
    if (fetch_valid_output) {
        if (decode_instruction == vpu::defs::SEGMENT_END)
            op += vpu::defs::SEGMENT_END_WIDTH_STRING;
        else
            op += vpu::defs::opcode_to_string_fixed(vpu::defs::get_opcode(decode_instruction));
    }else {
        op += na;
    }
    op += " | ";
    op += decode_valid_output ? vpu::defs::opcode_to_string_fixed(execute_opcode) : na;
    op += " | ";
    op += execute_valid_output ? vpu::defs::opcode_to_string_fixed(memory_opcode) : na;
    op += " | ";
    op += memory_valid_output ? vpu::defs::opcode_to_string_fixed(writeback_opcode) : na;
    op += " | ";
    op += writeback_valid_output ? vpu::defs::opcode_to_string_fixed(done_opcode) : na;
    op += " |";

    return op;
}

std::string ManagerCore::trace_string() {
    std::string op;
    for (int i = 0; i < vpu::defs::REGISTER_COUNT; i++){
        op += vpu::defs::register_to_string((vpu::defs::Register)i);
        op +=  " " + std::to_string(registers[i]) + " \t";
    }
    for (int i = 0; i < vpu::defs::FLAG_COUNT; i++){
        op += vpu::defs::flag_to_string((vpu::defs::Flag)i);
        op += " " + std::to_string(flags[i]) + " \t";
    }
    return op;
}

}
