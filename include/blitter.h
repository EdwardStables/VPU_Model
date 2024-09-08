#pragma once

#include <functional>

#include "defs_pkg.h"
#include "memory.h"

namespace vpu {

class Blitter {
    
public:
    enum Operation {
        NONE,
        CLEAR,
        PIXEL
    };
    struct Command {
        uint32_t xpos;
        uint32_t ypos;
        uint32_t colour;
        Operation operation = Blitter::NONE;
    };

private:
    std::unique_ptr<vpu::mem::Memory>& memory;
    enum {
        IDLE,
        WORKING,
        FINISHED
    } state = IDLE;

    uint32_t work_cycle;
    Command working_command;
    std::function<void()> working_callback;
    std::function<void()> finished_callback;
    bool finished_callback_valid = false;

    uint32_t next_address();
    void pixel_cycle();
    void clear_cycle();
public:
    bool submit(Command command, std::function<void()> completion_callback);
    Blitter(std::unique_ptr<vpu::mem::Memory>& memory);
    void run_cycle();

};

}
