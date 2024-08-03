#include "scheduler.h"
#include <assert.h>
#include <iostream>

namespace vpu {

Scheduler::Scheduler(DMA& dma, Blitter& blitter)
    : dma(dma), blitter(blitter)
{
 
}

bool Scheduler::submit_dma(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2) {
    switch(opcode) {
        case vpu::defs::P_DMA_DST_R:
            core_dma_frontend_state.dest = val1;
            return true;
        case vpu::defs::P_DMA_SRC_R:
            core_dma_frontend_state.source = val1;
            return true;
        case vpu::defs::P_DMA_LEN_R:
            core_dma_frontend_state.length = val1;
            return true;
        case vpu::defs::P_DMA_SET_R:
            core_dma_frontend_state.value = val1;
            core_dma_frontend_state.operation = DMA::SET;
            break;
        case vpu::defs::P_DMA_CPY:
            core_dma_frontend_state.operation = DMA::SET;
            break;
        default:
            std::cerr << "Scheduler error for opcode " << vpu::defs::opcode_to_string(opcode);
            std::cerr << " in DMA pipe." << pipe;
            assert(false);
    }


    //TODO need to confirm if this is actually correct RE cycle execution
    if (dma_frontend_queue.size() >= vpu::defs::SCHEDULER_FRONTEND_QUEUE_SIZE) {
        return false;
    }

    //When there is space, copy the frontend into the queue
    dma_frontend_queue.push_back({vpu::defs::get_next_global_cycle(),core_dma_frontend_state});
    return true;
}

bool Scheduler::core_submit(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2) {
    vpu::defs::Pipe pipe = vpu::defs::opcode_to_pipe(opcode);
    switch(pipe){
        case vpu::defs::DMA:
            return submit_dma(valid_cycle, opcode, val1, val2);
            break;
        default:
            std::cerr << "Scheduler error for opcode " << vpu::defs::opcode_to_string(opcode);
            std::cerr << " No implementation for pipe " << pipe;
            assert(false);

    }
    assert(false);
    return false;
}

void Scheduler::run_cycle() {

}

}