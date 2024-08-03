#pragma once
#include <array>
#include <memory>
#include <deque>
#include <tuple>

#include "config.h"
#include "memory.h"
#include "defs_pkg.h"
#include "scheduler.h"

namespace vpu {

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
    std::array<bool,vpu::defs::BHT_SIZE> bht;
    std::array<uint32_t,vpu::defs::BTB_SIZE> btb;
    bool has_halted;
    void update_pc(uint32_t new_pc);
    void set_flag(vpu::defs::Flag flag);
    void unset_flag(vpu::defs::Flag flag);
    bool get_flag(vpu::defs::Flag flag);
    uint32_t PC();

    Scheduler& scheduler;

    /* Stages */
    //Instruction Fetch
    void stage_fetch();
    bool fetch_seen_hlt = false;
    
    //Instruction Decode
    //cycle,instruction,pc,nextpc
    std::deque<std::tuple<uint32_t,uint32_t,uint32_t,uint32_t>> decode_input_queue;
    void stage_decode();

    //probably only 2 entries ever needed, but all keeps it simpler for now
    //TODO: this will be cleared when the same entry reaches writeback commit 
    //      with same value, may need a smarter system
    std::array<bool,vpu::defs::REGISTER_COUNT> execute_feedback_reg_held;
    std::array<uint32_t,vpu::defs::REGISTER_COUNT> execute_feedback_reg_value;

    //Execution
    //cycle,opcode,dest,source0,source1,nextpc
    std::deque<std::tuple<
        uint32_t, //cycle
        vpu::defs::Opcode, //opcode
        vpu::defs::Register, //dest
        uint32_t, //source0
        uint32_t, //source1
        uint32_t, //pc
        uint32_t  //next pc
    >> execute_input_queue;
    void stage_execute();
    std::deque<std::pair<uint32_t,uint32_t>> flush_queue;

    //Memory Access
    std::deque<std::tuple<
        uint32_t, //cycle
        vpu::defs::Opcode, //opcode
        bool, //write
        vpu::defs::Register, //Dest
        uint32_t //value
        //TODO flags
    >> memory_input_queue;
    void stage_memory();

    //Writeback
    //Memory Access
    std::deque<std::tuple<
        uint32_t, //cycle
        vpu::defs::Opcode, //opcode
        bool, //write
        vpu::defs::Register, //Dest
        uint32_t //value
        //TODO flags
    >> writeback_input_queue;
    void stage_writeback();
    bool writeback_valid = false;
    vpu::defs::Opcode writeback_opcode;
    /* End stages */    

    //Status printing
    std::string pipeline_string();
    std::string pipeline_heading();
    std::string trace_string();

public:
    ManagerCore(
        vpu::config::Config& config,
        std::unique_ptr<vpu::mem::Memory>& memory,
        Scheduler& scheduler
    );
    void run_cycle();
    bool check_has_halted();
    void print_status_start();
    void print_status(uint32_t cycle=0);
};

}