#include <iostream>
#include <assert.h>
#include <optional>

#include "defs_pkg.h"
#include "manager_core.h"
#include "stb_image_write.h"

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
    registers[vpu::defs::SP] = 0x100000; //Start of writable region
    flags.fill(0);
    btb.fill(0xDEADBEEF);
    bht.fill(false);
    execute_feedback_reg_held.fill(false);
    execute_feedback_reg_value.fill(0);
}

void ManagerCore::stage_pc(uint32_t new_pc) {
    potential_next_pc = new_pc;
}

void ManagerCore::update_pc() {
    registers[vpu::defs::PC] = potential_next_pc;
}

void ManagerCore::run_cycle() {
    if (PC() == 0)
        registers[vpu::defs::PC] = memory->read_word(0); //Starting instr addr stored at 0

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

    status_fetch_opcode     = "";
    status_decode_opcode    = "";
    status_execute_opcode   = "";
    status_memory_opcode    = "";
    status_writeback_opcode = "";

    //Stall set by execute, therefore this one applies on the following cycle
                      stage_fetch(frontend_stall, flush_valid, flush_addr);
    if (!flush_valid) stage_decode(frontend_stall);
    if (!flush_valid) stage_execute();
                      stage_memory();
                      stage_writeback();

    //Delay run cycle for a stall
    if (frontend_stall) {
        for (auto& d : decode_input_queue) d.increment();
        for (auto& e : execute_input_queue) e.increment();
    }

    //Queue and PC updates happen at the end of the current cycle
    if (!frontend_stall) update_pc();
    if (!frontend_stall && decode_input_queue.size()    && decode_input_queue.front().can_run()   ) decode_input_queue.pop_front();
    if (!frontend_stall && execute_input_queue.size()   && execute_input_queue.front().can_run()  ) execute_input_queue.pop_front();
    if (                   memory_input_queue.size()    && memory_input_queue.front().can_run()   ) memory_input_queue.pop_front();
    if (                   writeback_input_queue.size() && writeback_input_queue.front().can_run()) writeback_input_queue.pop_front();
}

void ManagerCore::stage_fetch(bool stall, bool flush_valid, uint32_t flush_addr) {
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

    status_fetch_opcode = vpu::defs::opcode_to_string_fixed(vpu::defs::get_opcode(decode_instruction));
    status_fetch_pc = pc;

    if (stall)
        return;

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

    decode_input_queue.push_back(DecodeInput{decode_instruction,pc,potential_next_pc});
}

void ManagerCore::stage_decode(bool stall) {
    if (decode_input_queue.empty() || !decode_input_queue.front().can_run()) return;

    assert(decode_input_queue.front().cycle == vpu::defs::get_global_cycle());
    auto input = decode_input_queue.front().data;

    if (input.instruction == vpu::defs::SEGMENT_END){
        has_halted = true;
        return;
    }

    //If above not triggered then the output must be valid
    auto decode_opcode = vpu::defs::get_opcode(input.instruction);
    uint8_t reg_index;

    vpu::defs::Register decode_dest = (vpu::defs::Register)0;
    uint32_t decode_source0 = 0;
    uint32_t decode_source2 = 0;

    //dest
    switch(decode_opcode) {
        //Nothing
        case vpu::defs::NOP:
        case vpu::defs::HLT:
        case vpu::defs::JMP_L:
        case vpu::defs::BRA_L:
        case vpu::defs::CMP_R:
        case vpu::defs::CMP_R_R:
        case vpu::defs::STW_R:
        case vpu::defs::STW_R_R:
        case vpu::defs::STW_R_I:
            break;
        //ACC dest
        case vpu::defs::MOV_I:
        case vpu::defs::ADD_I:
        case vpu::defs::ADD_R:
        case vpu::defs::ASR_I:
        case vpu::defs::LSR_I:
        case vpu::defs::LSL_I:
        case vpu::defs::ASR_R:
        case vpu::defs::LSR_R:
        case vpu::defs::LSL_R:
        case vpu::defs::LDW_R:
        case vpu::defs::LDW_R_R:
        case vpu::defs::LDW_R_I:
            decode_dest = vpu::defs::ACC;
            break;
        //Register destination
        case vpu::defs::MOV_R_I:
        case vpu::defs::MOV_R_R:
        case vpu::defs::LBA_R_B:
            decode_dest = vpu::defs::get_register(input.instruction,0);
            break;
        //Pipes
        //Nothing
        case vpu::defs::P_SCH_FNC:
        case vpu::defs::P_DMA_DST_R:
        case vpu::defs::P_DMA_SRC_R:
        case vpu::defs::P_DMA_LEN_R:
        case vpu::defs::P_DMA_SET_R:
        case vpu::defs::P_DMA_CPY:
        case vpu::defs::P_BLI_CLR:
        case vpu::defs::P_BLI_PIX_R_R:
        case vpu::defs::P_BLI_COL_R:
        case vpu::defs::P_BLI_COL_I:
        case vpu::defs::P_BLI_SPS_R_R:
        case vpu::defs::P_BLI_SPC_R:
        case vpu::defs::P_BLI_SPC_I:
        case vpu::defs::P_BLI_SWP:
        case vpu::defs::P_REN_STR_R:
        case vpu::defs::P_REN_TRN_R:
        case vpu::defs::P_MAT_SRC1_R_I:
        case vpu::defs::P_MAT_SRC1_R_R:
        case vpu::defs::P_MAT_SRC2_R_I:
        case vpu::defs::P_MAT_SRC2_R_R:
        case vpu::defs::P_MAT_DST_R_I:
        case vpu::defs::P_MAT_ROW_R:
        case vpu::defs::P_MAT_ROW_I:
        case vpu::defs::P_MAT_COL_R:
        case vpu::defs::P_MAT_COL_I:
        case vpu::defs::P_MAT_OPR_I:
            break;
        default:
            std::cerr << "Error decoding opcode " << vpu::defs::opcode_to_string(decode_opcode);
            std::cerr << " at address " << std::hex << PC();
            std::cerr << " for dest operand" << std::endl;
            assert(false);
    }
    //source0
    switch(decode_opcode) {
        //Nothing
        case vpu::defs::NOP:
        case vpu::defs::HLT:
            break;
        //Immediate 24-bit
        case vpu::defs::MOV_I:
        case vpu::defs::ADD_I:
        case vpu::defs::ASR_I:
        case vpu::defs::LSR_I:
        case vpu::defs::LSL_I:
            decode_source0 = get_int_literal(input.instruction);
            break;
        //Register destination
        case vpu::defs::MOV_R_I:
            decode_source0 = get_int_literal(input.instruction);
            break;
        case vpu::defs::LBA_R_B:
            decode_source0 = get_blob_literal(input.instruction);
            break;
        case vpu::defs::CMP_R:
        case vpu::defs::ADD_R:
        case vpu::defs::ASR_R:
        case vpu::defs::LSR_R:
        case vpu::defs::LSL_R:
        case vpu::defs::STW_R:
        case vpu::defs::LDW_R:
        case vpu::defs::STW_R_R:
        case vpu::defs::STW_R_I:
        case vpu::defs::LDW_R_R:
        case vpu::defs::LDW_R_I:
        case vpu::defs::CMP_R_R:
            decode_source0 = (uint32_t)vpu::defs::get_register(input.instruction,0);
            break;
        case vpu::defs::MOV_R_R:
            decode_source0 = (uint32_t)vpu::defs::get_register(input.instruction,1);
            break;
        //Label
        case vpu::defs::JMP_L:
        case vpu::defs::BRA_L:
            decode_source0 = vpu::defs::get_label(input.instruction);
            break;
        //Pipes
        //Nothing
        case vpu::defs::P_SCH_FNC:
        case vpu::defs::P_DMA_CPY:
        case vpu::defs::P_BLI_CLR:
        case vpu::defs::P_BLI_SWP:
            break;
        //I source
        case vpu::defs::P_BLI_COL_I:
        case vpu::defs::P_BLI_SPC_I:
        case vpu::defs::P_MAT_ROW_I:
        case vpu::defs::P_MAT_COL_I:
            decode_source0 = get_int_literal(input.instruction);
            break;
        case vpu::defs::P_MAT_OPR_I:
            decode_source0 = vpu::defs::ACC; //take value in ACC
            break;
        //Register source
        case vpu::defs::P_DMA_DST_R:
        case vpu::defs::P_DMA_SRC_R:
        case vpu::defs::P_DMA_LEN_R:
        case vpu::defs::P_DMA_SET_R:
        case vpu::defs::P_BLI_COL_R:
        case vpu::defs::P_BLI_PIX_R_R:
        case vpu::defs::P_BLI_SPS_R_R:
        case vpu::defs::P_BLI_SPC_R:
        case vpu::defs::P_REN_STR_R:
        case vpu::defs::P_REN_TRN_R:
        case vpu::defs::P_MAT_SRC1_R_I:
        case vpu::defs::P_MAT_SRC1_R_R:
        case vpu::defs::P_MAT_SRC2_R_I:
        case vpu::defs::P_MAT_SRC2_R_R:
        case vpu::defs::P_MAT_DST_R_I:
        case vpu::defs::P_MAT_ROW_R:
        case vpu::defs::P_MAT_COL_R:
            decode_source0 = (uint32_t)vpu::defs::get_register(input.instruction,0);
            break;
        default:
            std::cerr << "Error decoding opcode " << vpu::defs::opcode_to_string(decode_opcode);
            std::cerr << " at address " << std::hex << PC();
            std::cerr << " source operand" << std::endl;
            assert(false);
    }
    //source1
    switch(decode_opcode) {
        //Nothing
        case vpu::defs::NOP:
        case vpu::defs::HLT:
        case vpu::defs::MOV_I:
        case vpu::defs::MOV_R_I:
        case vpu::defs::MOV_R_R:
        case vpu::defs::JMP_L:
        case vpu::defs::BRA_L:
        case vpu::defs::LBA_R_B:
            break;
        case vpu::defs::STW_R:
        case vpu::defs::LDW_R:
            decode_source2 = 0;
            break;
        //applied to ACC
        case vpu::defs::ADD_I:
        case vpu::defs::ASR_I:
        case vpu::defs::LSR_I:
        case vpu::defs::LSL_I:
        case vpu::defs::ASR_R:
        case vpu::defs::LSR_R:
        case vpu::defs::LSL_R:
        case vpu::defs::ADD_R:
            decode_source2 = (uint32_t)vpu::defs::ACC;
            break;
        case vpu::defs::CMP_R_R:
        case vpu::defs::STW_R_R:
        case vpu::defs::LDW_R_R:
            decode_source2 = (uint32_t)vpu::defs::get_register(input.instruction,1);
            break;
        case vpu::defs::CMP_R:
            decode_source2 = 0;
            break;
        case vpu::defs::STW_R_I:
        case vpu::defs::LDW_R_I:
            decode_source2 = (uint32_t)get_int_literal(input.instruction);
            break;
        //Pipes
        case vpu::defs::P_SCH_FNC:
        case vpu::defs::P_DMA_CPY:
        case vpu::defs::P_DMA_DST_R:
        case vpu::defs::P_DMA_SRC_R:
        case vpu::defs::P_DMA_LEN_R:
        case vpu::defs::P_DMA_SET_R:
        case vpu::defs::P_BLI_COL_R:
        case vpu::defs::P_BLI_COL_I:
        case vpu::defs::P_BLI_CLR:
        case vpu::defs::P_BLI_SPC_R:
        case vpu::defs::P_BLI_SPC_I:
        case vpu::defs::P_BLI_SWP:
        case vpu::defs::P_REN_STR_R:
        case vpu::defs::P_REN_TRN_R:
        case vpu::defs::P_MAT_ROW_R:
        case vpu::defs::P_MAT_ROW_I:
        case vpu::defs::P_MAT_COL_R:
        case vpu::defs::P_MAT_COL_I:
            break;
        case vpu::defs::P_MAT_SRC1_R_I:
        case vpu::defs::P_MAT_SRC2_R_I:
        case vpu::defs::P_MAT_DST_R_I:
        case vpu::defs::P_MAT_OPR_I:
            decode_source2 = (uint32_t)get_int_literal(input.instruction);
            break;
        case vpu::defs::P_BLI_PIX_R_R:
        case vpu::defs::P_BLI_SPS_R_R:
        case vpu::defs::P_MAT_SRC1_R_R:
        case vpu::defs::P_MAT_SRC2_R_R:
            decode_source2 = (uint32_t)vpu::defs::get_register(input.instruction,1);
            break;
        default:
            std::cerr << "Error decoding opcode " << vpu::defs::opcode_to_string(decode_opcode);
            std::cerr << " at address " << std::hex << PC();
            std::cerr << " source operand" << std::endl;
            assert(false);
    }

    status_decode_opcode = vpu::defs::opcode_to_string_fixed(decode_opcode);
    status_decode_pc = input.pc;
    if (!stall) {
        execute_input_queue.push_back(ExecuteInput{
                decode_opcode,
                decode_dest,
                decode_source0,
                decode_source2,
                input.pc,
                input.next_pc
            }
        );
    }
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
        case vpu::defs::MOV_I:
        case vpu::defs::ADD_I:
        case vpu::defs::ASR_I:
        case vpu::defs::LSR_I:
        case vpu::defs::LSL_I:
        case vpu::defs::MOV_R_I:
        case vpu::defs::JMP_L:
        case vpu::defs::BRA_L:
        case vpu::defs::LBA_R_B:
            source_value0 = input.source0;
            break;
        case vpu::defs::CMP_R:
        case vpu::defs::ASR_R:
        case vpu::defs::LSR_R:
        case vpu::defs::LSL_R:
        case vpu::defs::LDW_R:
        case vpu::defs::STW_R:
        case vpu::defs::ADD_R:
        case vpu::defs::LDW_R_I:
        case vpu::defs::STW_R_I:
        case vpu::defs::LDW_R_R:
        case vpu::defs::STW_R_R:
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
        case vpu::defs::P_BLI_CLR:
        case vpu::defs::P_BLI_SWP:
            break;
        //I
        case vpu::defs::P_BLI_COL_I:
        case vpu::defs::P_BLI_SPC_I:
        case vpu::defs::P_MAT_ROW_I:
        case vpu::defs::P_MAT_COL_I:
            source_value0 = input.source0;
            break;
        //Register
        case vpu::defs::P_DMA_DST_R:
        case vpu::defs::P_DMA_SRC_R:
        case vpu::defs::P_DMA_LEN_R:
        case vpu::defs::P_DMA_SET_R:
        case vpu::defs::P_BLI_COL_R:
        case vpu::defs::P_BLI_PIX_R_R:
        case vpu::defs::P_BLI_SPC_R:
        case vpu::defs::P_BLI_SPS_R_R:
        case vpu::defs::P_REN_STR_R:
        case vpu::defs::P_REN_TRN_R:
        case vpu::defs::P_MAT_SRC1_R_I:
        case vpu::defs::P_MAT_SRC1_R_R:
        case vpu::defs::P_MAT_SRC2_R_I:
        case vpu::defs::P_MAT_SRC2_R_R:
        case vpu::defs::P_MAT_DST_R_I:
        case vpu::defs::P_MAT_ROW_R:
        case vpu::defs::P_MAT_COL_R:
        case vpu::defs::P_MAT_OPR_I: //OPR gets ACC as a register
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
        case vpu::defs::MOV_I:
        case vpu::defs::MOV_R_I:
        case vpu::defs::MOV_R_R:
        case vpu::defs::JMP_L:
        case vpu::defs::BRA_L:
        case vpu::defs::CMP_R:
        case vpu::defs::STW_R:
        case vpu::defs::LDW_R:
        case vpu::defs::LDW_R_I:
        case vpu::defs::STW_R_I:
        case vpu::defs::LBA_R_B:
            source_value1 = input.source1;
            break;
        //applied to ACC
        case vpu::defs::ADD_I:
        case vpu::defs::ADD_R:
        case vpu::defs::ASR_I:
        case vpu::defs::LSR_I:
        case vpu::defs::LSL_I:
        case vpu::defs::ASR_R:
        case vpu::defs::LSR_R:
        case vpu::defs::LSL_R:
        case vpu::defs::LDW_R_R:
        case vpu::defs::STW_R_R:
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
        case vpu::defs::P_BLI_CLR:
        case vpu::defs::P_BLI_COL_R:
        case vpu::defs::P_BLI_COL_I:
        case vpu::defs::P_BLI_SPC_R:
        case vpu::defs::P_BLI_SPC_I:
        case vpu::defs::P_BLI_SWP:
        case vpu::defs::P_REN_STR_R:
        case vpu::defs::P_REN_TRN_R:
        case vpu::defs::P_MAT_ROW_R:
        case vpu::defs::P_MAT_ROW_I:
        case vpu::defs::P_MAT_COL_R:
        case vpu::defs::P_MAT_COL_I:
            break;
        case vpu::defs::P_MAT_OPR_I:
        case vpu::defs::P_MAT_DST_R_I:
        case vpu::defs::P_MAT_SRC2_R_I:
        case vpu::defs::P_MAT_SRC1_R_I:
            source_value1 = input.source1;
            break;
        case vpu::defs::P_BLI_PIX_R_R:
        case vpu::defs::P_BLI_SPS_R_R:
        case vpu::defs::P_MAT_SRC1_R_R:
        case vpu::defs::P_MAT_SRC2_R_R:
            source_value1 = execute_feedback_reg_held[input.source1] ?
                                    execute_feedback_reg_value[input.source1] :
                                    registers[input.source1];
            break;
        default:
            std::cerr << "Error decoding opcode " << vpu::defs::opcode_to_string(input.opcode);
            std::cerr << " at address " << std::hex << PC();
            std::cerr << " source operand" << std::endl;
            assert(false);
    }

    //Store instructions have three sources, but one of them is always ACC
    //Therefore we can explicitly save it here to maintain the above structure
    uint32_t acc_value = execute_feedback_reg_held[vpu::defs::ACC] ?
                            execute_feedback_reg_value[vpu::defs::ACC] :
                            registers[vpu::defs::ACC];
    //Also calculate the sum to simplify case statement for store/load
    uint32_t mem_access_address = source_value0 + source_value1; 
    
    uint32_t memory_next_pc = input.pc + 4;
    uint32_t unsigned_temp;
    int32_t signed_temp;
    bool pipeline_submit = false;
    switch(input.opcode) {
        case vpu::defs::NOP:
        case vpu::defs::HLT: //HLT is not actually applied until the final stage to ensure full writeback completes
            break;
        case vpu::defs::MOV_R_I:
        case vpu::defs::MOV_R_R:
        case vpu::defs::MOV_I:
        case vpu::defs::LBA_R_B:
            memory_reg_index = input.dest;
            memory_reg_value = source_value0;
            break;
        case vpu::defs::ADD_R:
        case vpu::defs::ADD_I:
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
        case vpu::defs::ASR_I:
        case vpu::defs::ASR_R:
            signed_temp = (int32_t)source_value1;
            signed_temp >>= source_value0;
            memory_reg_index = input.dest;
            memory_reg_value = signed_temp;
            break;
        case vpu::defs::LSR_I:
        case vpu::defs::LSR_R:
            unsigned_temp = (int32_t)source_value1;
            unsigned_temp >>= source_value0;
            memory_reg_index = input.dest;
            memory_reg_value = unsigned_temp;
            break;
        case vpu::defs::LSL_R:
        case vpu::defs::LSL_I:
            unsigned_temp = (int32_t)source_value1;
            unsigned_temp <<= source_value0;
            memory_reg_index = input.dest;
            memory_reg_value = unsigned_temp;
            break;
        case vpu::defs::BRA_L:
            if (get_flag(vpu::defs::C))
                memory_next_pc = source_value0;
            else
                memory_next_pc = input.next_pc;
            break;
        case vpu::defs::JMP_L:
            memory_next_pc = source_value0;
            break;
        case vpu::defs::STW_R:
        case vpu::defs::STW_R_R:
        case vpu::defs::STW_R_I:
            memory->write_word(mem_access_address, acc_value);
            break;
        case vpu::defs::LDW_R:
        case vpu::defs::LDW_R_R:
        case vpu::defs::LDW_R_I:
            memory_reg_index = input.dest;
            memory_reg_value = memory->read_word(mem_access_address);
            break;
        //Pipeline instructions handled in scheduler
        default:
            assert((uint32_t)input.opcode >= 128); //pipeline instructions have a different opcode range
            pipeline_submit = true;
    }

    //Do before the stall
    status_execute_opcode = vpu::defs::opcode_to_string_fixed(input.opcode);
    status_execute_pc = input.pc;

    //Branch pred miss check before commiting any actual data
    //HLT has a special case of next_pc where it is set to be the same as the current PC, therefore exlude it specifically
    if (input.next_pc != memory_next_pc && input.opcode != vpu::defs::HLT){
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
    
    //After stalling and branch prediction checks are complete we can actually submit data to the hardware pipes
    if (pipeline_submit) {
        bool successful_submit = scheduler.core_submit(vpu::defs::get_next_global_cycle(), input.opcode, source_value0, source_value1);
        //Scheduler stall
        if (!successful_submit){
            frontend_stall = true; 
            return;
        }

        //Framebuffer dump
        if (input.opcode == vpu::defs::P_BLI_SWP && config.dump_framebuffer.length() != 0) {
            write_framebuffer();
        }
    }
    frontend_stall = false;

    if (memory_reg_index != (vpu::defs::Register)0){
        execute_feedback_reg_held[(size_t)memory_reg_index] = true;
        execute_feedback_reg_value[(size_t)memory_reg_index] = memory_reg_value;
    }

    memory_input_queue.push_back(MemoryInput{memory_opcode, memory_reg_index!=0, input.pc, memory_reg_index, memory_reg_value});
}

void ManagerCore::stage_memory() {
    /*
    This stage currently does nothing due to the simplistic memory implementation
    Memory accesses occur in the previous cycle, and in reality the data would not return until this stage


    Once a proper memory heirarchy is simulated, this stage will receive the data. For cached data this should lead to no stalls,
    for non-cached data this stage will stall on reads until data returns.
    */

    if (memory_input_queue.empty() || !memory_input_queue.front().can_run()) return;

    assert(memory_input_queue.front().cycle == vpu::defs::get_global_cycle());
    auto input = memory_input_queue.front().data;

    status_memory_opcode = vpu::defs::opcode_to_string_fixed(input.opcode);
    status_memory_pc = input.pc;
    writeback_input_queue.push_back(WritebackInput{input.opcode, input.write, input.pc, input.dest, input.value});
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
    status_writeback_opcode = vpu::defs::opcode_to_string_fixed(input.opcode);
    status_writeback_pc = input.pc;
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
        h.insert(0, vpu::defs::MAX_OPCODE_LEN + std::string(" (0x00000000)").length() - h.size(), ' ');
        op += h;
        op += " |";
    }

    return op;
}

std::string ManagerCore::pipeline_string() {
    std::stringstream op;    
    std::string na(vpu::defs::MAX_OPCODE_LEN + std::string(" (0x00000000)").length(), '-');
    uint32_t cycle = vpu::defs::get_global_cycle();
    op << "| ";
    if (status_fetch_opcode.length()) {
        op << status_fetch_opcode;
        op << " (0x" << std::setfill('0') << std::setw(8) << std::hex << status_fetch_pc << ")";
    } else {
        op << na;
    }
    op << " | ";
    if (status_decode_opcode.length()) {
        op << status_decode_opcode;
        op << " (0x" << std::setfill('0') << std::setw(8) << std::hex << status_decode_pc << ")";
    } else {
        op << na;
    }
    op << " | ";
    if (status_execute_opcode.length()) {
        op << status_execute_opcode;
        op << " (0x" << std::setfill('0') << std::setw(8) << std::hex << status_execute_pc << ")";
    } else {
        op << na;
    }
    op << " | ";
    if (status_memory_opcode.length()) {
        op << status_memory_opcode;
        op << " (0x" << std::setfill('0') << std::setw(8) << std::hex << status_memory_pc << ")";
    } else {
        op << na;
    }
    op << " | ";
    if (writeback_valid) {
        op << status_writeback_opcode;
        op << " (0x" << std::setfill('0') << std::setw(8) << std::hex << status_writeback_pc << ")";
    } else {
        op << na;
    }
    op << " |";

    return op.str();
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

void ManagerCore::write_framebuffer() {
    std::stringstream output_file_ss;
    output_file_ss <<  config.dump_framebuffer + "/frame_";
    output_file_ss << std::setw(5) << std::setfill('0') << std::to_string(frames_written);
    output_file_ss << ".png";
    std::string output_file = output_file_ss.str();
    std::cout << "Writing out framebuffer to " << output_file << std::endl;
    
    int x = vpu::defs::FRAMEBUFFER_WIDTH;
    int y = vpu::defs::FRAMEBUFFER_HEIGHT;
    int comp = 4; //RGBA
    int stride = vpu::defs::FRAMEBUFFER_WIDTH * vpu::defs::FRAMEBUFFER_PIXEL_BYTES;

    auto& all_mem = vpu::mem::MemorySnooper::get_data(memory.get());
    void* data = (&(all_mem[0])) + vpu::defs::FRAMEBUFFER_ADDR;

    int r = stbi_write_png(output_file.c_str(), x, y, comp, data, stride);
    if (r == 0) {
        std::cerr << "Failed to write frame " << frames_written << std::endl;
    }
    frames_written++;
}

uint32_t ManagerCore::get_int_literal(uint32_t instruction) {
    uint32_t index = instruction & 0x0000FFFF;

    return memory->read_word(vpu::defs::LITERAL_TABLE_ADDR + (4*index));
}

uint32_t ManagerCore::get_blob_literal(uint32_t instruction) {
    return memory->read_word(instruction & 0x0000FFFF);
}

}
