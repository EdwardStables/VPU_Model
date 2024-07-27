#pragma once
#include <array>
#include <memory>
#include <optional>

#include "config.h"
#include "memory.h"
#include "defs_pkg.h"

namespace vpu::core {

class ManagerCore;
class ManagerCoreSnooper {
public:
    ManagerCoreSnooper() = delete;
    static uint32_t get_register(ManagerCore& core, vpu::defs::Register reg);
};

class ManagerCore {
    friend ManagerCoreSnooper;
    std::array<uint32_t,vpu::defs::REGISTER_COUNT> registers;
    vpu::config::Config& config;
    std::unique_ptr<vpu::mem::Memory>& memory;
    std::array<bool,vpu::defs::FLAG_COUNT> flags;
    bool has_halted;
    void update_pc(uint32_t new_pc);
    void set_flag(vpu::defs::Flag flag);
    void unset_flag(vpu::defs::Flag flag);
    bool get_flag(vpu::defs::Flag flag);
    uint32_t PC();


    /* Stages */
    //Instruction Fetch
    void stage_fetch();
    bool fetch_valid_output = false;
    bool fetch_seen_hlt = false;
    uint32_t decode_instruction;
    uint32_t decode_next_pc;
    
    //Instruction Decode
    void stage_decode();
    bool decode_valid_output = false;
    vpu::defs::Opcode execute_opcode;
    vpu::defs::Register execute_dest;
    uint32_t execute_source0;
    uint32_t execute_source1;
    uint32_t execute_next_pc;

    //probably only 2 entries ever needed, but all keeps it simpler for now
    //TODO: this will be cleared when the same entry reaches writeback commit 
    //      with same value, may need a smarter system
    std::array<bool,vpu::defs::REGISTER_COUNT> execute_feedback_reg_held;
    std::array<uint32_t,vpu::defs::REGISTER_COUNT> execute_feedback_reg_value;

    //Execution
    void stage_execute();
    bool execute_valid_output = false;
    bool flush_set = false; 
    vpu::defs::Opcode memory_opcode;
    uint32_t flush_next_pc;
    vpu::defs::Register memory_reg_index;
    uint32_t memory_reg_value;

    //Memory Access
    void stage_memory();
    bool memory_valid_output = false;
    vpu::defs::Opcode writeback_opcode;
    vpu::defs::Register writeback_reg_index;
    uint32_t writeback_reg_value;

    //Writeback
    void stage_writeback();
    bool writeback_valid_output = false;
    vpu::defs::Opcode done_opcode;
    vpu::defs::Register writeback_commit_reg_index;
    uint32_t writeback_commit_reg_value;
    void stage_writeback_commit();
    /* End stages */    

    //Status printing
    std::string pipeline_string();
    std::string pipeline_heading();
    std::string trace_string();

public:
    ManagerCore(vpu::config::Config& config, std::unique_ptr<vpu::mem::Memory>& memory) ;
    void run_cycle();
    bool check_has_halted();
    void print_status_start();
    void print_status(uint32_t cycle=0);
};

}