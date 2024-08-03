#include <iostream>

#include "dma.h"

namespace vpu {

DMA::DMA(std::unique_ptr<vpu::mem::Memory>& memory) :
    memory(memory)
{

}

bool DMA::submit(DMA::Command command, std::function<void()> completion_callback) {
    completion_callback();
    return false;
}

void DMA::run_cycle(){

}

}