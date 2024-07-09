#pragma once
#include <array>
#include <memory>

#include "config.h"
#include "memory.h"
#include "defs_pkg.h"

namespace vpu::core {

class ManagerCore {
    std::array<uint32_t,vpu::defs::REGISTER_COUNT> registers;
    vpu::config::Config& config;
    std::unique_ptr<vpu::mem::Memory>& memory;
    bool has_halted;
    void update_pc(uint32_t new_pc);
public:
    ManagerCore(vpu::config::Config& config, std::unique_ptr<vpu::mem::Memory>& memory) ;
    void run_cycle();
    bool check_has_halted();
    uint32_t PC();
};

}