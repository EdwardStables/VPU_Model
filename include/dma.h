#pragma once
#include <memory>
#include <functional>

#include "defs_pkg.h"
#include "memory.h"
#include "subsystem.h"

namespace vpu::dma {

enum class Operation {
    NONE,
    COPY,
    SET
};

struct Command
{
    uint32_t dest;
    uint32_t source;
    uint32_t length;
    uint8_t value;
    Operation operation=Operation::NONE;
};

class DMA : public Subsystem<Command> {
    std::unique_ptr<vpu::mem::Memory>& memory;
    uint32_t write_pointer;
    uint32_t read_pointer;
    bool fetched_writeback_data_valid = false;
    std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> fetched_writeback_data;

    bool active_buffer_valid = false;
    uint32_t active_buffer_address = 0;
    uint32_t active_buffer_size = 0;
    std::array<uint8_t,2*vpu::defs::MEM_ACCESS_WIDTH> active_buffer;

    void copy_cycle();
    void set_cycle();
public:
    DMA(std::unique_ptr<vpu::mem::Memory>& memory);
    virtual bool submit();
    virtual void run_cycle();
};

}
