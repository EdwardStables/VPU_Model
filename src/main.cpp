#include <iostream>
#include "memory.h"
#include <memory>

int main() {
    auto memory = std::make_unique<vpu::mem::Memory>(); 
    memory->write(1234, 0xDEADBEEF);
    std::cout << std::hex << memory->read(1234) << std::endl;
    return 0;
}