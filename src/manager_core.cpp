#include <iostream>
#include <assert.h>
#include <optional>

#include "manager_core.h"

namespace vpu {

uint32_t ManagerCoreSnooper::get_register(ManagerCore& core, vpu::defs::Register reg) {
    return core.registers[reg];
}

ManagerCore::ManagerCore(
    vpu::config::Config& config,
    std::unique_ptr<vpu::mem::Memory>& memory,
    Scheduler& scheduler
) :
    config(config),
    memory(memory),
    scheduler(scheduler),
    has_halted(false)
{
    registers.fill(0);
    flags.fill(0);
    btb.fill(0xDEADBEEF);
    bht.fill(false);
}

void ManagerCore::stage_pc(uint32_t new_pc) {
    potential_next_pc = new_pc;
}

void ManagerCore::update_pc() {
    registers[vpu::defs::PC] = potential_next_pc;
}

void ManagerCore::run_cycle() {
    //Flush always happen
    uint32_t flush_addr = 0;
    bool flush_valid = false;
    if (!flush_queue.empty()){
        auto flush_cycle = flush_queue.front().cycle;
        //In case we had no valid input for previous flush cycle
        if (vpu::defs::get_global_cycle() >= flush_cycle){
            flush_valid = true;
            flush_addr = flush_queue.front().data;
            flush_queue.pop_front();
        }
    }


    //Stall set by execute, therefore this one applies on the following cycle
    if (!frontend_stall)                 stage_fetch(flush_valid, flush_addr);
    if (!frontend_stall && !flush_valid) stage_decode();
    if (                   !flush_valid) stage_execute();
                                         stage_memory();
                                         stage_writeback();

    //Queue and PC updates happen at the end of the current cycle
    if (!frontend_stall) update_pc();
    if (!frontend_stall && decode_input_queue.size()    && decode_input_queue.front().can_run()   ) decode_input_queue.pop_front();
    if (!frontend_stall && execute_input_queue.size()   && execute_input_queue.front().can_run()  ) execute_input_queue.pop_front();
    if (                   memory_input_queue.size()    && memory_input_queue.front().can_run()   ) memory_input_queue.pop_front();
    if (                   writeback_input_queue.size() && writeback_input_queue.front().can_run()) writeback_input_queue.pop_front();
}

void ManagerCore::stage_fetch(bool flush_valid, uint32_t flush_addr) {
    //When we've hit a HLT and have not seen a flush then do not dispatch more instructions
    if (fetch_seen_hlt && !flush_valid){ 
        return;
    }
    
    //Handle flushing
    fetch_seen_hlt = false;
    
    uint32_t pc = PC();
    uint32_t next_pc;
    //Don't pop flush queue because decode stage still needs to read it
    if (flush_valid){
        pc = flush_addr;
    }

    uint32_t decode_instruction = memory->read_word(pc);

    //Halt after flush to retain correct final PC on HLT flush
    if (has_halted){
        return;
    }

    //Don't increment PC or end output for segment end.
    if (vpu::defs::get_opcode(decode_instruction) == vpu::defs::HLT){
        fetch_seen_hlt = true;
        //flush pc change needs to be propagated to the register
        stage_pc(pc);
    } else {
        uint32_t bht_tag = vpu::defs::get_bht_tag(pc);
        if (bht[bht_tag]) {
            uint32_t btb_tag = vpu::defs::get_btb_tag(pc);
            next_pc = btb[btb_tag];
            assert(bht_tag != 0xDEADBEEF);
        } else {
            next_pc = pc + 4;
        }
        stage_pc(next_pc);
    }

    decode_input_queue.push_back(DecodeInput{decode_instruction,pc,PC()});
}

void ManagerCore::stage_decode() {
    if (decode_input_queue.empty() || !decode_input_queue.front().can_run()) return;

    assert(decode_input_queue.front().cycle == vpu::defs::get_global_cycle());
    auto input = decode_input_queue.front().data;

    if (input.instruction == vpu::defs::SEGMENT_END){
        has_halted = true;
        return;
    }

    //If above not triggered then the output must be valid
    auto execute_opcode = vpu::defs::get_opcode(input.instruction);
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
            execute_dest = vpu::defs::get_register(input.instruction,0);
            break;
        
        //Pipes
        //Nothing
        case vpu::defs::P_SCH_FNC:
        case vpu::defs::P_DMA_DST_R:
        case vpu::defs::P_DMA_SRC_R:
        case vpu::defs::P_DMA_LEN_R:
        case vpu::defs::P_DMA_SET_R:
        case vpu::defs::P_DMA_CPY:
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
            execute_source0 = vpu::defs::get_u24(input.instruction);
            break;
        //Register destination
        case vpu::defs::MOV_R_I16:
            execute_source0 = vpu::defs::get_u16(input.instruction);
            break;
        case vpu::defs::CMP_R:
        case vpu::defs::ASR_R:
        case vpu::defs::LSR_R:
        case vpu::defs::LSL_R:
            execute_source0 = (uint32_t)vpu::defs::get_register(input.instruction,0);
            break;
        case vpu::defs::CMP_R_R:
        case vpu::defs::MOV_R_R:
            execute_source0 = (uint32_t)vpu::defs::get_register(input.instruction,1);
            break;
        //Label
        case vpu::defs::JMP_L:
        case vpu::defs::BRA_L:
            execute_source0 = vpu::defs::get_label(input.instruction);
            break;
        //Pipes
        //Nothing
        case vpu::defs::P_SCH_FNC:
        case vpu::defs::P_DMA_CPY:
            break;
        //Register source
        case vpu::defs::P_DMA_DST_R:
        case vpu::defs::P_DMA_SRC_R:
        case vpu::defs::P_DMA_LEN_R:
        case vpu::defs::P_DMA_SET_R:
            execute_source0 = (uint32_t)vpu::defs::get_register(input.instruction,0);
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
            execute_source1 = (uint32_t)vpu::defs::get_register(input.instruction,0);
            break;
        case vpu::defs::CMP_R:
            execute_source1 = 0;
        //Pipes
        case vpu::defs::P_SCH_FNC:
        case vpu::defs::P_DMA_CPY:
        case vpu::defs::P_DMA_DST_R:
        case vpu::defs::P_DMA_SRC_R:
        case vpu::defs::P_DMA_LEN_R:
        case vpu::defs::P_DMA_SET_R:
            break;
        default:
            std::cerr << "Error decoding opcode " << vpu::defs::opcode_to_string(execute_opcode);
            std::cerr << " at address " << std::hex << PC();
            std::cerr << " source operand" << std::endl;
            assert(false);
    }

    execute_input_queue.push_back(ExecuteInput{
            execute_opcode,
            execute_dest,
            execute_source0,
            execute_source1,
            input.pc,
            input.next_pc
        }
    );
}

void ManagerCore::stage_execute() {
    if (execute_input_queue.empty() || !execute_input_queue.front().can_run()) return;

    auto input = execute_input_queue.front().data;
    assert(execute_input_queue.front().cycle == vpu::defs::get_global_cycle());

    vpu::defs::Register memory_reg_index = (vpu::defs::Register)0; //indicated PC, which is invalid and will be ignored
    uint32_t memory_reg_value = 0;
    vpu::defs::Opcode memory_opcode = input.opcode;

    uint32_t source_value0 = 0;
    uint32_t source_value1 = 0;    

    //source0
    switch(input.opcode) {
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
            source_value0 = input.source0;
            break;
        case vpu::defs::CMP_R:
        case vpu::defs::ASR_R:
        case vpu::defs::LSR_R:
        case vpu::defs::LSL_R:
        case vpu::defs::CMP_R_R:
        case vpu::defs::MOV_R_R:
            source_value0 = execute_feedback_reg_held[input.source0] ?
                                    execute_feedback_reg_value[input.source0] :
                                    registers[input.source0];
            break;
        //Pipes
        //Nothing
        case vpu::defs::P_SCH_FNC:
        case vpu::defs::P_DMA_CPY:
            break;
        //Register
        case vpu::defs::P_DMA_DST_R:
        case vpu::defs::P_DMA_SRC_R:
        case vpu::defs::P_DMA_LEN_R:
        case vpu::defs::P_DMA_SET_R:
            source_value0 = execute_feedback_reg_held[input.source0] ?
                                    execute_feedback_reg_value[input.source0] :
                                    registers[input.source0];
            break;
        default:
            std::cerr << "Error decoding opcode " << vpu::defs::opcode_to_string(input.opcode);
            std::cerr << " at address " << std::hex << PC();
            std::cerr << " source operand" << std::endl;
            assert(false);
    }
    //source1
    switch(input.opcode) {
        //Nothing
        case vpu::defs::NOP:
        case vpu::defs::HLT:
        case vpu::defs::MOV_I24:
        case vpu::defs::MOV_R_I16:
        case vpu::defs::MOV_R_R:
        case vpu::defs::JMP_L:
        case vpu::defs::BRA_L:
        case vpu::defs::CMP_R:
            source_value1 = input.source1;
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
            source_value1 = execute_feedback_reg_held[input.source1] ?
                                    execute_feedback_reg_value[input.source1] :
                                    registers[input.source1];
            break;
        //Pipes
        case vpu::defs::P_SCH_FNC:
        case vpu::defs::P_DMA_CPY:
        case vpu::defs::P_DMA_DST_R:
        case vpu::defs::P_DMA_SRC_R:
        case vpu::defs::P_DMA_LEN_R:
        case vpu::defs::P_DMA_SET_R:
            break;
        default:
            std::cerr << "Error decoding opcode " << vpu::defs::opcode_to_string(input.opcode);
            std::cerr << " at address " << std::hex << PC();
            std::cerr << " source operand" << std::endl;
            assert(false);
    }
    
    bool check_flush = false;
    bool successful_submit = true;
    uint32_t memory_next_pc;
    uint32_t unsigned_temp;
    int32_t signed_temp;
    switch(input.opcode) {
        case vpu::defs::NOP:
        case vpu::defs::HLT: //HLT is not actually applied until the final stage to ensure full writeback completes
            break;
        case vpu::defs::MOV_R_I16:
        case vpu::defs::MOV_R_R:
        case vpu::defs::MOV_I24:
            memory_reg_index = input.dest;
            memory_reg_value = source_value0;
            break;
        case vpu::defs::ADD_I24:
            memory_reg_index = input.dest;
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
            memory_reg_index = input.dest;
            memory_reg_value = signed_temp;
            break;
        case vpu::defs::LSR_I24:
        case vpu::defs::LSR_R:
            unsigned_temp = (int32_t)source_value1;
            unsigned_temp >>= source_value0;
            memory_reg_index = input.dest;
            memory_reg_value = unsigned_temp;
            break;
        case vpu::defs::LSL_R:
        case vpu::defs::LSL_I24:
            unsigned_temp = (int32_t)source_value1;
            unsigned_temp <<= source_value0;
            memory_reg_index = input.dest;
            memory_reg_value = unsigned_temp;
            break;
        case vpu::defs::BRA_L:
            check_flush = true;
            if (get_flag(vpu::defs::C))
                memory_next_pc = source_value0;
            else
                memory_next_pc = input.next_pc;
            break;
        case vpu::defs::JMP_L:
            check_flush = true;
            memory_next_pc = source_value0;
            break;
        //Pipeline instructions handled in scheduler
        default:
            assert((uint32_t)input.opcode >= 128); //pipeline instructions have a different opcode range
            successful_submit = scheduler.core_submit(vpu::defs::get_next_global_cycle(), input.opcode, source_value0, source_value1);
    }

    //Scheduler stall
    if (!successful_submit){
        frontend_stall = true; 
        return;
    }
    frontend_stall = false;

    if (memory_reg_index != (vpu::defs::Register)0){
        execute_feedback_reg_held[(size_t)memory_reg_index] = true;
        execute_feedback_reg_value[(size_t)memory_reg_index] = memory_reg_value;
    }

    if (check_flush) {
        //branch pred miss
        if (input.next_pc != memory_next_pc){
            //missed on fallthrough
            if (input.next_pc == input.pc + 4){
                bht[vpu::defs::get_bht_tag(input.pc)] = true; //look up the actual destination in the btb
                btb[vpu::defs::get_btb_tag(input.pc)] = memory_next_pc;
            }
            
            //Must flush to resolve the misprediction
            flush_queue.push_back(memory_next_pc);
        } 
        
        //branch pred hit 
        else {
            //fallthrough
            if (input.next_pc == input.pc + 4){
                bht[vpu::defs::get_bht_tag(input.pc)] = false; //let it carry on
            }
        }
    }

    memory_input_queue.push_back(MemoryInput{memory_opcode, memory_reg_index!=0, memory_reg_index, memory_reg_value});
}

void ManagerCore::stage_memory() {
    //TODO: Implement memory accessing

    if (memory_input_queue.empty() || !memory_input_queue.front().can_run()) return;

    assert(memory_input_queue.front().cycle == vpu::defs::get_global_cycle());
    auto input = memory_input_queue.front().data;

    writeback_input_queue.push_back(WritebackInput{input.opcode, input.write, input.dest, input.value});
}

void ManagerCore::stage_writeback() {
    if (writeback_input_queue.empty() || !writeback_input_queue.front().can_run()) {
        writeback_valid = false;
        return;
    }

    assert(writeback_input_queue.front().cycle == vpu::defs::get_global_cycle());
    auto input = writeback_input_queue.front().data;

    writeback_valid = true;
    writeback_opcode = input.opcode;

    if (input.opcode == vpu::defs::HLT)    
        has_halted = true;

    if (input.write)
        registers[input.dest] = input.value;
    if (execute_feedback_reg_value[input.dest] == input.value) {
        execute_feedback_reg_held[input.dest] = false;
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

    if (config.pipeline || config.trace)
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
    uint32_t cycle = vpu::defs::get_global_cycle();
    op += "| ";
    if (!decode_input_queue.empty() && decode_input_queue.front().can_run()) {
        auto instr = decode_input_queue.front().data.instruction;
        if (instr == vpu::defs::SEGMENT_END)
            op += vpu::defs::SEGMENT_END_WIDTH_STRING;
        else
            op += vpu::defs::opcode_to_string_fixed(vpu::defs::get_opcode(instr));
    } else {
        op += na;
    }
    op += " | ";
    if (!execute_input_queue.empty() && execute_input_queue.front().can_run())
        op += vpu::defs::opcode_to_string_fixed(execute_input_queue.front().data.opcode);
    else
        op += na;
    op += " | ";
    if (!memory_input_queue.empty() && memory_input_queue.front().can_run())
        op += vpu::defs::opcode_to_string_fixed(memory_input_queue.front().data.opcode);
    else
        op += na;
    op += " | ";
    if (!writeback_input_queue.empty() && writeback_input_queue.front().can_run())
        op += vpu::defs::opcode_to_string_fixed(writeback_input_queue.front().data.opcode);
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
