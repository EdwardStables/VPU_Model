#pragma once

#include <functional>

#include "defs_pkg.h"
#include "memory.h"
#include "subsystem.h"

namespace vpu::blit {

enum class Operation {
    NONE,
    CLEAR,
    PIXEL,
    STRING,
};

struct Command {
    uint32_t xpos;
    uint32_t ypos;
    uint32_t colour;
    uint8_t character;
    uint8_t character_vscan;
    Operation operation = Operation::NONE;
};

class Blitter : public Subsystem<Command> {
    std::unique_ptr<vpu::mem::Memory>& memory;
    uint8_t char_mask(uint8_t character, uint8_t vscan);
    uint32_t pixel_address(uint32_t x, uint32_t y);
    void pixel_cycle();
    void string_cycle();
    void clear_cycle();
public:
    Blitter(std::unique_ptr<vpu::mem::Memory>& memory);
    virtual bool submit();
    virtual void run_cycle();
};

}
