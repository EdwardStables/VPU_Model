#include "memory.h"
#include <assert.h>
#include <algorithm>

namespace vpu::mem {

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