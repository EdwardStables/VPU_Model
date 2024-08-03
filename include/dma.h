#pragma once
#include <memory>
#include <functional>

#include "defs_pkg.h"
#include "memory.h"

namespace vpu {

class DMA {
    std::unique_ptr<vpu::mem::Memory>& memory;
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
    bool submit(Command command, std::function<void()> completion_callback);
    DMA(std::unique_ptr<vpu::mem::Memory>& memory);
    void run_cycle();
};

}
