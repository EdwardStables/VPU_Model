#include "blitter.h"
#include "defs_pkg.h"
#include <algorithm>
#include <assert.h>
#include <cstdint>
#include <iostream>

namespace vpu {

//Calculate the address of the next pixel coordinate in the working_command
uint32_t Blitter::next_address() {
    uint32_t x = working_command.xpos;
    uint32_t y = working_command.ypos;
    assert(x < defs::FRAMEBUFFER_WIDTH);
    assert(y < defs::FRAMEBUFFER_HEIGHT);
    uint32_t offset = x * defs::FRAMEBUFFER_PIXEL_BYTES;
    offset += y * defs::FRAMEBUFFER_PIXEL_BYTES * defs::FRAMEBUFFER_WIDTH;
    assert(offset < defs::FRAMEBUFFER_BYTES); //ensure calculated address is within framebuffer
    return offset + defs::FRAMEBUFFER_ADDR;
}

void Blitter::pixel_cycle() {
    memory->write_word(next_address(), working_command.colour);
    state = FINISHED;
}

void Blitter::clear_cycle() {
    std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> data;
    assert(vpu::defs::MEM_ACCESS_WIDTH == 4 * vpu::defs::BLITTER_MAX_PIXELS);

    uint32_t write_addr = next_address();
    assert((write_addr & 0x3F) == 0); //for now only allow 512-bit aligned writes

    for (int i = 0; i < defs::BLITTER_MAX_PIXELS; i++) {
        data[4*i]   = working_command.colour >> 24;
        data[4*i+1] = (working_command.colour >> 16) & 0xFF;
        data[4*i+2] = (working_command.colour >> 8) & 0xFF;
        data[4*i+3] = working_command.colour & 0xFF;
    }
    memory->write(next_address(), data);

    //Will overwrite end of buffer, but that should be ok for now
    working_command.xpos += defs::BLITTER_MAX_PIXELS;
    while (working_command.xpos >= defs::FRAMEBUFFER_WIDTH) {
        working_command.xpos -= defs::FRAMEBUFFER_WIDTH;
        working_command.ypos++;
    }
    if (working_command.ypos >= defs::FRAMEBUFFER_HEIGHT)
        state = FINISHED;
}

void Blitter::run_cycle(){
    if (finished_callback_valid) {
        finished_callback();
        finished_callback_valid = false;
    }

    if (state == IDLE) return;
    if (state == FINISHED) {
        state = IDLE;
        return;
    }

    if (vpu::defs::get_global_cycle() < work_cycle) return;

    switch(working_command.operation) {
        case PIXEL: pixel_cycle(); break;
        case CLEAR: clear_cycle(); break;

        default:
            std::cerr << "Invalid Blitter operation ";
            assert(false);
    }
}

Blitter::Blitter(std::unique_ptr<vpu::mem::Memory>& memory)
    : memory(memory)
{
}

bool Blitter::submit(Command command, std::function<void()> completion_callback) {
    if (state == WORKING) {
        return false;
    }

    assert(command.operation != NONE);

    state = WORKING;
    work_cycle = vpu::defs::get_next_global_cycle();
    working_command = command;
    working_callback = completion_callback;
    if (working_command.operation == CLEAR){
        working_command.xpos = 0;
        working_command.ypos = 0;
    }
    
    return true;
}

}
