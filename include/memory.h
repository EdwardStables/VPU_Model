#pragma once

#include <memory>
#include <cstdint>
#include <array>
#include <cstdio>
#include <fstream>
#include <filesystem>

#include "defs_pkg.h"

namespace vpu::mem {

//512MB
constexpr uint32_t MEM_SIZE = 512 * 1024 * 1024;
class Memory;

class MemorySnooper {
public:
    MemorySnooper() = delete;
    static void copy_file_in(std::unique_ptr<Memory>& memory, std::filesystem::path file);
    static uint8_t get_byte(std::unique_ptr<Memory>& memory, uint32_t index);
    static std::array<uint8_t,MEM_SIZE>& get_data(std::unique_ptr<Memory>& memory);
};

class Memory {
    friend MemorySnooper;
    std::array<uint8_t,MEM_SIZE> data;
public:
    Memory();
    uint32_t read_word(uint32_t addr);
    void write_word(uint32_t addr, uint32_t data);
    std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> read(uint32_t addr);
    void write(uint32_t addr, std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> data);
};

}