#pragma once
#include <memory>
#include <functional>

#include "defs_pkg.h"
#include "memory.h"

namespace vpu {

class DMA {
public:
    enum Operation {
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
        Operation operation=DMA::NONE;
    };
private:
    std::unique_ptr<vpu::mem::Memory>& memory;
    enum {
        IDLE,
        WORKING,
        FINISHED
    } state;
    uint32_t work_cycle;
    Command working_command;
    std::function<void()> working_callback;
    std::function<void()> finished_callback;
    uint32_t write_pointer;
    uint32_t read_pointer;
    std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> fetched_data;
    bool fetched_data_valid = false;

    void copy_cycle();
    void set_cycle();
public:
    bool submit(Command command, std::function<void()> completion_callback);
    DMA(std::unique_ptr<vpu::mem::Memory>& memory);
    void run_cycle();
};

}
