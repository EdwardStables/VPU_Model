#pragma once
#include <array>
#include <memory>

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
public:
    ManagerCore(vpu::config::Config& config, std::unique_ptr<vpu::mem::Memory>& memory) ;
    void run_cycle();
    bool check_has_halted();
    void print_trace(uint32_t cycle=0);
};

}