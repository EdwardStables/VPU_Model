#include "blitter.h"
#include "defs_pkg.h"
#include <algorithm>
#include <assert.h>
#include <cstdint>
#include <iostream>

namespace vpu {

//Calculate the address of the next pixel coordinate in the working_command
uint32_t Blitter::pixel_address(uint32_t x, uint32_t y) {
    assert(x < defs::FRAMEBUFFER_WIDTH);
    assert(y < defs::FRAMEBUFFER_HEIGHT);
    uint32_t offset = x * defs::FRAMEBUFFER_PIXEL_BYTES;
    offset += y * defs::FRAMEBUFFER_PIXEL_BYTES * defs::FRAMEBUFFER_WIDTH;
    assert(offset < defs::FRAMEBUFFER_BYTES); //ensure calculated address is within framebuffer
    return offset + defs::FRAMEBUFFER_ADDR;
}

void Blitter::pixel_cycle() {
    memory->write_word(pixel_address(working_command.xpos, working_command.ypos), working_command.colour);
    state = FINISHED;
}

uint8_t Blitter::char_mask(uint8_t character, uint8_t vscan) {
    //Each character is eight 8bit masks stored from bottom row to top. Therefore each supported ascii character
    //is stored in a uint64_t
    
    const uint64_t all_masks[] = {
      //space             !                 "                 #                 $
        0,                0x183C3C18180018, 0x6C6C6C00000000, 0x6C6CFE6CFE6C6C, 0x307CC0780CF830,
      //%               &                 '                   (                 )
        0x00C6CC183066C6, 0x3C6C3876DCCC76, 0x6060C000000000, 0x18306060603018, 0x60301818183060, 
      //*                 +                 `                 -                 .                 /
        0x667EFF7E660000, 0x3030FC30300000, 0x30301800000000, 0x0000FE00000000, 0x00000000001818, 0x60C183060C080,
      //0                 1                 2                 3                 4
        0x7CC6CEDEF6E67C, 0x307030303030FE, 0x78CC0C3860CCFC, 0x78CC0C380CCC78, 0xC3C6CCCFE0C1E,
      //5                 6                 7                 8                 9
        0xFEC0F80C0CCC78, 0x3860C0F8CCCC78, 0xFCCC0C18303030, 0x78CCCC78CCCC78, 0x78CC3C7C0C1870

    };

    
    const uint32_t index = character - 32;
    
    //while the full table is not yet built, hard-code the invalid input value
    const uint64_t full_character_mask = (character == 127) ? 0xFF818181818181FF : all_masks[index];
    const uint8_t line_mask = (full_character_mask >> (8*(7-vscan))) & 0xFF;
    return line_mask;
}

void Blitter::string_cycle() {
    if (working_command.character_vscan == 8) {
        state = FINISHED;
        return;
    }

    uint32_t char_line_address = pixel_address(working_command.xpos, working_command.ypos+working_command.character_vscan);
    uint32_t cacheline_address = char_line_address & ~uint32_t(0x3F);
    uint32_t cacheline_offset = char_line_address - cacheline_address;


    std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> working_data;
    working_data = memory->read(cacheline_address);

    uint8_t char_line_mask = char_mask(working_command.character, working_command.character_vscan);
    uint8_t index_offset = 0;

    for (int i = 0; i < 8; i++) {
        if (((char_line_mask >> (7-i)) & 1) == 0) continue;

        //If character line crosses fetch boundary then writeback and re-fetch
        if (cacheline_offset + (4*(i-index_offset)) >= 64) {
            memory->write(cacheline_address, working_data);
            char_line_address = pixel_address(working_command.xpos+i, working_command.ypos+working_command.character_vscan);
            cacheline_address = char_line_address & ~uint32_t(0x3F);
            cacheline_offset = char_line_address - cacheline_address;
            working_data = memory->read(cacheline_address);
            //Account for the changed boundaries
            index_offset = i;
        }

        working_data[cacheline_offset+(4*(i-index_offset))  ] =  working_command.colour >> 24;
        working_data[cacheline_offset+(4*(i-index_offset))+1] = (working_command.colour >> 16) & 0xFF;
        working_data[cacheline_offset+(4*(i-index_offset))+2] = (working_command.colour >> 8) & 0xFF;
        working_data[cacheline_offset+(4*(i-index_offset))+3] =  working_command.colour & 0xFF;
    }

    memory->write(cacheline_address, working_data);
    working_command.character_vscan++;
}

void Blitter::clear_cycle() {
    std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> data;
    assert(vpu::defs::MEM_ACCESS_WIDTH == 4 * vpu::defs::BLITTER_MAX_PIXELS);

    if (working_command.ypos >= defs::FRAMEBUFFER_HEIGHT) {
        state = FINISHED;
        return;
    }

    for (int i = 0; i < defs::BLITTER_MAX_PIXELS; i++) {
        data[4*i]   = working_command.colour >> 24;
        data[4*i+1] = (working_command.colour >> 16) & 0xFF;
        data[4*i+2] = (working_command.colour >> 8) & 0xFF;
        data[4*i+3] = working_command.colour & 0xFF;
    }

    uint32_t write_addr = pixel_address(working_command.xpos, working_command.ypos);
    assert((write_addr & 0x3F) == 0); //for now only allow 512-bit aligned writes
    memory->write(write_addr, data);

    //Will overwrite end of buffer, but that should be ok for now
    working_command.xpos += defs::BLITTER_MAX_PIXELS;
    while (working_command.xpos >= defs::FRAMEBUFFER_WIDTH) {
        working_command.xpos -= defs::FRAMEBUFFER_WIDTH;
        working_command.ypos++;
    }
}

void Blitter::run_cycle(){
    if (finished_callback_valid) {
        finished_callback();
        finished_callback_valid = false;
    }

    if (state == IDLE) {
        return;
    }
    if (state == FINISHED) {
        state = IDLE;
        return;
    }

    if (vpu::defs::get_global_cycle() < work_cycle) return;

    switch(working_command.operation) {
        case PIXEL: pixel_cycle(); break;
        case STRING: string_cycle(); break;
        case CLEAR: clear_cycle(); break;

        default:
            std::cerr << "Invalid Blitter operation ";
            assert(false);
    }

    if (state == FINISHED) {
        finished_callback = working_callback;
        finished_callback_valid = true;
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
    if (working_command.operation == STRING){
        working_command.character_vscan = 0;
    }
    
    return true;
}

}
