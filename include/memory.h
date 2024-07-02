#include <cstdint>
#include <array>
#include <cstdio>

namespace vpu::mem {

class Snooper {
    //TODO
};

//512MB
constexpr uint32_t MEM_SIZE = 512 * 1024 * 1024;

class Memory {
    friend Snooper;
    std::array<uint8_t,MEM_SIZE> data;
public:
    Memory();
    uint32_t read(uint32_t addr);
    void write(uint32_t addr, uint32_t data);
    void copy(uint32_t source, uint32_t dest, uint32_t size);
    void clear(uint32_t addr, uint32_t size, uint8_t byte=0);
};

}