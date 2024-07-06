#include "memory.h"
#include <assert.h>
#include <algorithm>
#include <iostream>


namespace vpu::mem {

uint8_t Snooper::get_byte(std::unique_ptr<Memory>& memory, uint32_t index) {
    return memory->data[index];
}

void Snooper::copy_file_in(std::unique_ptr<Memory>& memory, std::filesystem::path file) {
    std::ifstream program(file, std::ios::binary | std::ios::in);
    if (!program.is_open()) {
        std::cerr << "Failed to open " << file << " for reading.";
        exit(1);
    }
    //Copying to a vector then array seems silly, but going directly to the array fails to compile
    std::vector<char> new_data{std::istreambuf_iterator<char>(program), std::istreambuf_iterator<char>()};
    std::copy(new_data.begin(), new_data.end(), memory->data.begin());
}

Memory::Memory() {
    clear(0, MEM_SIZE);
}

uint32_t Memory::read(uint32_t addr) {
    addr &= 0xFFFFFFFC;
    uint32_t ret = 0;
    for (size_t i = 0; i<4; i++)
        ret |= data[addr+i] << (i*8);
    return ret;
}
void Memory::write(uint32_t addr, uint32_t data) {
    addr &= 0xFFFFFFFC;
    for (size_t i = 0; i<4; i++)
        this->data[addr+i] = 0xFF & (data >> 8*i);
}

void Memory::copy(uint32_t source, uint32_t dest, uint32_t size) {
    assert(source + size - 1 < MEM_SIZE);
    assert(dest + size - 1 < MEM_SIZE);

    for (size_t i = 0; i < size; i++){
        data[dest+i] = data[source+i];
    }
}

void Memory::clear(uint32_t addr, uint32_t size, uint8_t byte) {
    assert(addr + size - 1 < MEM_SIZE);

    std::fill(data.begin(), data.begin()+size, byte);
}

}