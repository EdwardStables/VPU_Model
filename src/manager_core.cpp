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

uint32_t ManagerCore::next_cycle() {
    assert(core_cycle_count < 0xFFFFFFFF);
    return core_cycle_count+1;
}

void ManagerCore::update_pc(uint32_t new_pc) {
    registers[vpu::defs::PC] = new_pc;
}

void ManagerCore::run_cycle() {
    stage_fetch();
    stage_decode();
    stage_execute();
    stage_memory();
    stage_writeback();
    core_cycle_count++;
}

void ManagerCore::stage_fetch() {
    //When we've hit a HLT and have not seen a flush then do not dispatch more instructions
    if (fetch_seen_hlt && (flush_queue.empty() || core_cycle_count < std::get<0>(flush_queue.front()))){
        return;
    }
    
    //Handle flushing
    fetch_seen_hlt = false;
    
    uint32_t pc = PC();
    
    //Don't pop flush queue because decode stage still needs to read it
    if (!flush_queue.empty()){
        auto [flush_cycle,flush_next_pc] = flush_queue.front();
        if (core_cycle_count == flush_cycle){
            pc = flush_next_pc;
        }
    }

    uint32_t decode_instruction = memory->read(pc);

    //Halt after flush to retain correct final PC on HLT flush
    if (has_halted){
        return;
    }

    //Don't increment PC or end output for segment end.
    if (vpu::defs::get_opcode(decode_instruction) == vpu::defs::HLT){
        fetch_seen_hlt = true;
    } else {
        update_pc(pc+4);
    }

    decode_input_queue.push_back({next_cycle(),decode_instruction,PC()});
}

void ManagerCore::stage_decode() {
    if (decode_input_queue.empty() || core_cycle_count < std::get<0>(decode_input_queue.front())) return;

    auto [target_cycle,instruction,next_pc] = decode_input_queue.front();
    decode_input_queue.pop_front();
    assert(target_cycle == core_cycle_count);

    if (!flush_queue.empty()){
        auto [flush_cycle,flush_next_pc] = flush_queue.front();
        //In case we had no valid input for previous flush cycle
        if (core_cycle_count >= flush_cycle)
            flush_queue.pop_front();
        //Only skip if the cycle matches exactly
        if (core_cycle_count == flush_cycle)
            return;
    }

    if (instruction == vpu::defs::SEGMENT_END){
        has_halted = true;
        return;
    }

    //If above not triggered then the output must be valid
    
    uint32_t execute_next_pc = next_pc;
    auto execute_opcode = vpu::defs::get_opcode(instruction);
    uint8_t reg_index;

    vpu::defs::Register execute_dest = (vpu::defs::Register)0;
    uint32_t execute_source0 = 0;
    uint32_t execute_source1 = 0;

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
            execute_dest = vpu::defs::get_register(instruction,0);
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
            execute_source0 = vpu::defs::get_u24(instruction);
            break;
        //Register destination
        case vpu::defs::MOV_R_I16:
            execute_source0 = vpu::defs::get_u16(instruction);
            break;
        case vpu::defs::CMP_R:
        case vpu::defs::ASR_R:
        case vpu::defs::LSR_R:
        case vpu::defs::LSL_R:
            execute_source0 = (uint32_t)vpu::defs::get_register(instruction,0);
            break;
        case vpu::defs::CMP_R_R:
        case vpu::defs::MOV_R_R:
            execute_source0 = (uint32_t)vpu::defs::get_register(instruction,1);
            break;
        //Label
        case vpu::defs::JMP_L:
        case vpu::defs::BRA_L:
            execute_source0 = vpu::defs::get_label(instruction);
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
            execute_source1 = (uint32_t)vpu::defs::ACC;
            break;
        case vpu::defs::CMP_R_R:
            execute_source1 = (uint32_t)vpu::defs::get_register(instruction,0);
            break;
        case vpu::defs::CMP_R:
            execute_source1 = 0;
        default:
            std::cerr << "Error decoding opcode " << vpu::defs::opcode_to_string(execute_opcode);
            std::cerr << " at address " << std::hex << PC();
            std::cerr << " source operand" << std::endl;
            assert(false);
    }

    execute_input_queue.push_back(
        {
            next_cycle(),
            execute_opcode,
            execute_dest,
            execute_source0,
            execute_source1,
            execute_next_pc
        }
    );
}

void ManagerCore::stage_execute() {
    if (execute_input_queue.empty() || core_cycle_count < std::get<0>(execute_input_queue.front())) return;

    auto [target_cycle, opcode, dest, source0, source1, next_pc] = execute_input_queue.front();
    execute_input_queue.pop_front();
    assert(target_cycle == core_cycle_count);

    vpu::defs::Register memory_reg_index = (vpu::defs::Register)0; //indicated PC, which is invalid and will be ignored
    uint32_t memory_reg_value = 0;
    vpu::defs::Opcode memory_opcode = opcode;

    uint32_t source_value0 = 0;
    uint32_t source_value1 = 0;    

    //source0
    switch(opcode) {
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
        case vpu::defs::MOV_R_I16:
        case vpu::defs::JMP_L:
        case vpu::defs::BRA_L:
            source_value0 = source0;
            break;
        case vpu::defs::CMP_R:
        case vpu::defs::ASR_R:
        case vpu::defs::LSR_R:
        case vpu::defs::LSL_R:
        case vpu::defs::CMP_R_R:
        case vpu::defs::MOV_R_R:
            source_value0 = execute_feedback_reg_held[source0] ?
                                    execute_feedback_reg_value[source0] :
                                    registers[source0];
            break;
        default:
            std::cerr << "Error decoding opcode " << vpu::defs::opcode_to_string(opcode);
            std::cerr << " at address " << std::hex << PC();
            std::cerr << " source operand" << std::endl;
            assert(false);
    }
    //source1
    switch(opcode) {
        //Nothing
        case vpu::defs::NOP:
        case vpu::defs::HLT:
        case vpu::defs::MOV_I24:
        case vpu::defs::MOV_R_I16:
        case vpu::defs::MOV_R_R:
        case vpu::defs::JMP_L:
        case vpu::defs::BRA_L:
        case vpu::defs::CMP_R:
            source_value1 = source1;
            break;
        //applied to ACC
        case vpu::defs::ADD_I24:
        case vpu::defs::ASR_I24:
        case vpu::defs::LSR_I24:
        case vpu::defs::LSL_I24:
        case vpu::defs::ASR_R:
        case vpu::defs::LSR_R:
        case vpu::defs::LSL_R:
        case vpu::defs::CMP_R_R:
            source_value1 = execute_feedback_reg_held[source1] ?
                                    execute_feedback_reg_value[source1] :
                                    registers[source1];
            break;
        default:
            std::cerr << "Error decoding opcode " << vpu::defs::opcode_to_string(opcode);
            std::cerr << " at address " << std::hex << PC();
            std::cerr << " source operand" << std::endl;
            assert(false);
    }
    
    bool check_flush = false;
    uint32_t memory_next_pc = next_pc;

    uint32_t unsigned_temp;
    int32_t signed_temp;
    switch(opcode) {
        case vpu::defs::NOP:
        case vpu::defs::HLT: //HLT is not actually applied until the final stage to ensure full writeback completes
            break;
        case vpu::defs::MOV_R_I16:
        case vpu::defs::MOV_R_R:
        case vpu::defs::MOV_I24:
            memory_reg_index = dest;
            memory_reg_value = source_value0;
            break;
        case vpu::defs::ADD_I24:
            memory_reg_index = dest;
            memory_reg_value = source_value0 + source_value1;
            break;
        case vpu::defs::CMP_R:
        case vpu::defs::CMP_R_R:
            //TODO does this need pipelining and going via writeback?
            if (source_value0 == source_value1)
                set_flag(vpu::defs::C);
            else
                unset_flag(vpu::defs::C);
            break;
        case vpu::defs::ASR_I24:
        case vpu::defs::ASR_R:
            signed_temp = (int32_t)source_value1;
            signed_temp >>= source_value0;
            memory_reg_index = dest;
            memory_reg_value = signed_temp;
            break;
        case vpu::defs::LSR_I24:
        case vpu::defs::LSR_R:
            unsigned_temp = (int32_t)source_value1;
            unsigned_temp >>= source_value0;
            memory_reg_index = dest;
            memory_reg_value = unsigned_temp;
            break;
        case vpu::defs::LSL_R:
        case vpu::defs::LSL_I24:
            unsigned_temp = (int32_t)source_value1;
            unsigned_temp <<= source_value0;
            memory_reg_index = dest;
            memory_reg_value = unsigned_temp;
            break;
        case vpu::defs::BRA_L:
            check_flush = true;
            if (get_flag(vpu::defs::C))
                next_pc = source_value0;
            break;
        case vpu::defs::JMP_L:
            check_flush = true;
            next_pc = source_value0;
            break;
        default:
            std::cerr << "Error on opcode " << vpu::defs::opcode_to_string(opcode);
            std::cerr << " at address " << std::hex << PC();
            std::cerr << " during execution" << std::endl;
            assert(false);
    }

    if (memory_reg_index != (vpu::defs::Register)0){
        execute_feedback_reg_held[(size_t)memory_reg_index] = true;
        execute_feedback_reg_value[(size_t)memory_reg_index] = memory_reg_value;
    }

    if (check_flush && (next_pc != memory_next_pc)){
        flush_queue.push_back({next_cycle(),next_pc});
    }

    memory_input_queue.push_back(
        {
            next_cycle(),
            memory_opcode,
            memory_reg_index != 0,
            memory_reg_index,
            memory_reg_value
        }
    );
}

void ManagerCore::stage_memory() {
    //TODO: Implement memory accessing

    if (memory_input_queue.empty() || core_cycle_count < std::get<0>(memory_input_queue.front())) return;

    auto [target_cycle, opcode, write, index, value] = memory_input_queue.front();
    memory_input_queue.pop_front();
    assert(target_cycle == core_cycle_count);

    writeback_input_queue.push_back({
        next_cycle(),
        opcode,
        write,
        index,
        value
    });
}

void ManagerCore::stage_writeback() {
    if (writeback_input_queue.empty() || core_cycle_count < std::get<0>(writeback_input_queue.front())) {
        writeback_valid = false;
        return;
    }

    auto [target_cycle, opcode, write, index, value] = writeback_input_queue.front();
    writeback_input_queue.pop_front();
    assert(target_cycle == core_cycle_count);

    writeback_valid = true;
    writeback_opcode = opcode;

    if (opcode == vpu::defs::HLT)    
        has_halted = true;

    if (write)
        registers[index] = value;
    if (execute_feedback_reg_value[index] == value) {
        execute_feedback_reg_held[index] = false;
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
    if (!decode_input_queue.empty() && std::get<0>(decode_input_queue.front())==core_cycle_count) {
        auto instr = std::get<1>(decode_input_queue.front());
        if (instr == vpu::defs::SEGMENT_END)
            op += vpu::defs::SEGMENT_END_WIDTH_STRING;
        else
            op += vpu::defs::opcode_to_string_fixed(vpu::defs::get_opcode(instr));
    } else {
        op += na;
    }
    op += " | ";
    if (!execute_input_queue.empty() && std::get<0>(execute_input_queue.front())==core_cycle_count)
        op += vpu::defs::opcode_to_string_fixed(std::get<1>(execute_input_queue.front()));
    else
        op += na;
    op += " | ";
    if (!memory_input_queue.empty() && std::get<0>(memory_input_queue.front())==core_cycle_count)
        op += vpu::defs::opcode_to_string_fixed(std::get<1>(memory_input_queue.front()));
    else
        op += na;
    op += " | ";
    if (!writeback_input_queue.empty() && std::get<0>(writeback_input_queue.front())==core_cycle_count)
        op += vpu::defs::opcode_to_string_fixed(std::get<1>(writeback_input_queue.front()));
    else
        op += na;
    op += " | ";
    op += writeback_valid ? vpu::defs::opcode_to_string_fixed(writeback_opcode) : na;
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
