#include "scheduler.h"
#include "defs_pkg.h"
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
            core_dma_frontend_state.operation = DMA::COPY;
            break;
        default:
            std::cerr << "Scheduler error for opcode " << vpu::defs::opcode_to_string(opcode);
            std::cerr << " in DMA pipe. ";
            assert(false);
    }


    //TODO need to confirm if this is actually correct RE cycle execution, same for other pipes
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
            return (dma_outstanding==0) && (blitter_outstanding==0);
        default:
            std::cerr << "Scheduler error for opcode " << vpu::defs::opcode_to_string(opcode);
            std::cerr << " in sched pipe. ";
            assert(false);
    }
}

bool Scheduler::submit_blitter(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2) {
    switch(opcode) {
        case vpu::defs::P_BLI_COL_R:
            core_blitter_frontend_state.colour = (val1 << 8) | 0xFF; //Value in RGB, but colours are RGBA
            return true;
        case vpu::defs::P_BLI_PIX_R_R:
            core_blitter_frontend_state.xpos = val1;
            core_blitter_frontend_state.ypos = val2;
            core_blitter_frontend_state.operation = Blitter::PIXEL; 
            break;
        case vpu::defs::P_BLI_CLR:
            core_blitter_frontend_state.operation = Blitter::CLEAR; 
            break;
        default:
            std::cerr << "Scheduler error for opcode " << vpu::defs::opcode_to_string(opcode);
            std::cerr << " in blitter pipe. ";
            assert(false);
    }

    //TODO need to confirm if this is actually correct RE cycle execution, same for other pipes
    if (dma_frontend_queue.size() >= vpu::defs::SCHEDULER_FRONTEND_QUEUE_SIZE) {
        return false;
    }

    //When there is space, copy the frontend into the queue
    blitter_frontend_queue.push_back(core_blitter_frontend_state);
    blitter_outstanding++;
    core_blitter_frontend_state.operation = Blitter::NONE;
    return true;
}

void Scheduler::blitter_complete() {
    assert(blitter_outstanding > 0);
    blitter_outstanding--;
}

void Scheduler::dma_complete() {
    assert(dma_outstanding > 0);
    dma_outstanding--;
}

bool Scheduler::core_submit(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2) {
    vpu::defs::Pipe pipe = vpu::defs::opcode_to_pipe(opcode);
    switch(pipe){
        case vpu::defs::DMA:
            return submit_dma(valid_cycle, opcode, val1, val2);
        case vpu::defs::SCHED:
            return submit_sched(valid_cycle, opcode, val1, val2);
        case vpu::defs::BLITTER:
            return submit_blitter(valid_cycle, opcode, val1, val2);
        default:
            std::cerr << "Scheduler error for opcode " << vpu::defs::opcode_to_string(opcode);
            std::cerr << " No implementation for pipe " << pipe << " ";
            assert(false);
    }
}

void Scheduler::run_cycle() {
    check_dma();
    check_blitter();
}

void Scheduler::check_blitter() {
    //*** Blitter ***//
    //Nothing there
    if (!blitter_frontend_queue.size()) return;
    //Can't run yet
    if (!blitter_frontend_queue.front().can_run()) return;

    //Blitter can accept data
    std::function<void()> callback = std::bind(&Scheduler::blitter_complete, this);
    if (blitter.submit(blitter_frontend_queue.front().data, callback)){
        blitter_frontend_queue.pop_front();
        return;
    }

    //Otherwise it couldn't accept, need to increment all the valid cycles in the queue
    for (auto& cmd : blitter_frontend_queue){
        cmd.increment();
    }
}

void Scheduler::check_dma() {
    //*** DMA ***//
    //Nothing there
    if (!dma_frontend_queue.size()) return;
    //Can't run yet
    if (!dma_frontend_queue.front().can_run()) return;

    //DMA can accept data
    std::function<void()> callback = std::bind(&Scheduler::dma_complete, this);
    if (dma.submit(dma_frontend_queue.front().data, callback)){
        dma_frontend_queue.pop_front();
        return;
    }

    //Otherwise it couldn't accept, need to increment all the valid cycles in the queue
    for (auto& cmd : dma_frontend_queue){
        cmd.increment();
    }

}

}
