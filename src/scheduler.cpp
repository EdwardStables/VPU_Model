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
            std::cerr << " in DMA pipe. ";
            assert(false);
    }


    //TODO need to confirm if this is actually correct RE cycle execution
    if (dma_frontend_queue.size() >= vpu::defs::SCHEDULER_FRONTEND_QUEUE_SIZE) {
        return false;
    }

    //When there is space, copy the frontend into the queue
    dma_frontend_queue.push_back(core_dma_frontend_state);
    dma_outstanding++;
    core_dma_frontend_state.operation = DMA::NONE;
    return true;
}

bool Scheduler::submit_sched(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2) {
    switch(opcode) {
        case vpu::defs::P_SCH_FNC:
            return (dma_outstanding==0) && (blit_outstanding==0);
        default:
            std::cerr << "Scheduler error for opcode " << vpu::defs::opcode_to_string(opcode);
            std::cerr << " in sched pipe. ";
            assert(false);
    }
}

bool Scheduler::core_submit(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2) {
    vpu::defs::Pipe pipe = vpu::defs::opcode_to_pipe(opcode);
    switch(pipe){
        case vpu::defs::DMA:
            return submit_dma(valid_cycle, opcode, val1, val2);
        case vpu::defs::SCHED:
            return submit_sched(valid_cycle, opcode, val1, val2);
        default:
            std::cerr << "Scheduler error for opcode " << vpu::defs::opcode_to_string(opcode);
            std::cerr << " No implementation for pipe " << pipe << " ";
            assert(false);
    }
}

void Scheduler::run_cycle() {
    //*** DMA ***//
    //Nothing there
    if (!dma_frontend_queue.size()) return;
    //Can't run yet
    if (!dma_frontend_queue.front().can_run()) return;

    //DMA can accept data
    if (dma.submit(dma_frontend_queue.front().data, [&outstanding = dma_outstanding](){outstanding--;})){
        dma_frontend_queue.pop_front();
        return;
    }
    //Otherwise it couldn't accept, need to increment all the valid cycles in the queue
    for (auto& cmd : dma_frontend_queue){
        cmd.increment();
    }

}

}