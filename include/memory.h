#pragma once

#include <memory>
#include <cstdint>
#include <array>
#include <cstdio>
#include <fstream>
#include <filesystem>

namespace vpu::mem {

class Memory;
class MemorySnooper {
public:
    MemorySnooper() = delete;
    static void copy_file_in(std::unique_ptr<Memory>& memory, std::filesystem::path file);
    static uint8_t get_byte(std::unique_ptr<Memory>& memory, uint32_t index);
};

//512MB
constexpr uint32_t MEM_SIZE = 512 * 1024 * 1024;

class Memory {
    friend MemorySnooper;
    std::array<uint8_t,MEM_SIZE> data;
public:
    Memory();
    uint32_t read(uint32_t addr);
    void write(uint32_t addr, uint32_t data);
    void copy(uint32_t source, uint32_t dest, uint32_t size);
    void clear(uint32_t addr, uint32_t size, uint8_t byte=0);
};

}