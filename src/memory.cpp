#include "memory.h"
#include "defs_pkg.h"
#include <assert.h>
#include <algorithm>
#include <iostream>


namespace vpu::mem {

uint8_t MemorySnooper::get_byte(std::unique_ptr<Memory>& memory, uint32_t index) {
    return memory->data[index];
}

void MemorySnooper::copy_file_in(std::unique_ptr<Memory>& memory, std::filesystem::path file) {
    std::ifstream program(file, std::ios::binary | std::ios::in);
    if (!program.is_open()) {
        std::cerr << "Failed to open " << file << " for reading.";
        exit(1);
    }
    //Copying to a vector then array seems silly, but going directly to the array fails to compile
    std::vector<char> new_data{std::istreambuf_iterator<char>(program), std::istreambuf_iterator<char>()};
    std::copy(new_data.begin(), new_data.end(), memory->data.begin());
}

std::array<uint8_t,vpu::defs::MEM_SIZE>& MemorySnooper::get_data(std::unique_ptr<Memory>& memory) {
    return memory->data;
}

Memory::Memory() {
    std::cout << vpu::defs::MEM_SIZE;
    std::fill(data.begin(), data.begin()+vpu::defs::MEM_SIZE, 0);
}

uint32_t Memory::read_word(uint32_t addr) {
    addr &= 0xFFFFFFFC;
    uint32_t ret = 0;
    for (size_t i = 0; i<4; i++)
        ret |= data[addr+i] << (i*8);
    return ret;
}
void Memory::write_word(uint32_t addr, uint32_t data) {
    addr &= 0xFFFFFFFC;
    for (size_t i = 0; i<4; i++)
        this->data[addr+i] = 0xFF & (data >> 8*i);
}

std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> Memory::read(uint32_t addr) {
    assert((addr & 0x3F) == 0); //Must be 64-byte aligned
    assert(addr <= vpu::defs::MEM_SIZE-vpu::defs::MEM_ACCESS_WIDTH); //Don't read from beyond the end
    std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> ret;
    std::copy(data.begin()+addr,data.begin()+addr+vpu::defs::MEM_ACCESS_WIDTH, ret.begin());
    return ret;
}

void Memory::write(uint32_t addr, std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> write_data) {
    assert((addr & 0x3F) == 0); //Must be 64-byte aligned
    assert(addr <= vpu::defs::MEM_SIZE-vpu::defs::MEM_ACCESS_WIDTH); //Don't write beyond the end
    std::copy(write_data.begin(), write_data.end(), data.begin() + addr);
}

}
