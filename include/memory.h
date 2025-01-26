#pragma once

#include <memory>
#include <cstdint>
#include <array>
#include <cstdio>
#include <fstream>
#include <filesystem>

#include "defs_pkg.h"

namespace vpu::mem {
class Memory;

//Snooper takes a pointer to memory to have maximum compatability with
//all places it may be used
class MemorySnooper {
public:
    MemorySnooper() = delete;
    static void copy_file_in(Memory* memory, std::filesystem::path file);
    static uint8_t get_byte(Memory* memory, uint32_t index);
    static std::array<uint8_t,vpu::defs::MEM_SIZE>& get_data(Memory* memory);
};

class Memory {
    friend MemorySnooper;
    std::array<uint8_t,vpu::defs::MEM_SIZE> data;
public:
    Memory();
    uint32_t read_word(uint32_t addr);
    void write_word(uint32_t addr, uint32_t data);
    void write_word_mask(uint32_t addr, uint32_t data, uint8_t mask);
    std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> read(uint32_t addr);
    void write(uint32_t addr, std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> data);
    void write_mask(uint32_t addr, std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> data, uint64_t mask);
};

}