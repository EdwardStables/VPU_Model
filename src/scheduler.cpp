#include "scheduler.h"
#include "defs_pkg.h"
#include <assert.h>
#include <iostream>

namespace vpu {

Scheduler::Scheduler(dma::DMA& dma, blit::Blitter& blitter, stream::StreamRenderer& renderer)
    : dma(dma), blitter(blitter), renderer(renderer)
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
            core_dma_frontend_state.operation = dma::Operation::SET;
            break;
        case vpu::defs::P_DMA_CPY:
            core_dma_frontend_state.operation = dma::Operation::COPY;
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
    core_dma_frontend_state.operation = dma::Operation::NONE;
    return true;
}

bool Scheduler::submit_sched(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2) {
    switch(opcode) {
        case vpu::defs::P_SCH_FNC:
            return (dma_outstanding==0) && (blitter_outstanding==0) && (renderer_outstanding==0);
        default:
            std::cerr << "Scheduler error for opcode " << vpu::defs::opcode_to_string(opcode);
            std::cerr << " in sched pipe. ";
            assert(false);
    }
}

bool Scheduler::submit_blitter(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2) {
    switch(opcode) {
        //Config instructions just setup state for the following instruction and don't actually submit anything
        case vpu::defs::P_BLI_COL_R:
        case vpu::defs::P_BLI_COL_I:
            core_blitter_frontend_state.colour = (val1 << 8) | 0xFF; //Value in RGB, but colours are RGBA
            return true;
        case vpu::defs::P_BLI_SPS_R_R:
            core_blitter_frontend_state.xpos = val1;
            core_blitter_frontend_state.ypos = val2;
            return true;
        case vpu::defs::P_BLI_SWP: //Eventually this may update internal pointers, for now it just triggers a dump in the main core
            return true;

        //Actual kicks to the blitter
        case vpu::defs::P_BLI_PIX_R_R:
            core_blitter_frontend_state.xpos = val1;
            core_blitter_frontend_state.ypos = val2;
            core_blitter_frontend_state.operation = blit::Operation::PIXEL; 
            break;
        case vpu::defs::P_BLI_CLR:
            core_blitter_frontend_state.operation = blit::Operation::CLEAR; 
            break;
        case vpu::defs::P_BLI_SPC_I:
        case vpu::defs::P_BLI_SPC_R:
            core_blitter_frontend_state.character = (uint8_t)val1;
            //Don't support some ASCII ranges, use DEL as a placeholder for unsupported values
            if (core_blitter_frontend_state.character > 127 || core_blitter_frontend_state.character < 32)
                core_blitter_frontend_state.character = 127;
            core_blitter_frontend_state.operation = blit::Operation::STRING; 
            break;
        default:
            std::cerr << "Scheduler error for opcode " << vpu::defs::opcode_to_string(opcode);
            std::cerr << " in blitter pipe. ";
            assert(false);
    }

    //TODO need to confirm if this is actually correct RE cycle execution, same for other pipes
    if (blitter_frontend_queue.size() >= vpu::defs::SCHEDULER_FRONTEND_QUEUE_SIZE) {
        return false;
    }

    //When there is space, copy the frontend into the queue
    blitter_frontend_queue.push_back(core_blitter_frontend_state);
    blitter_outstanding++;
    core_blitter_frontend_state.operation = blit::Operation::NONE;

    //Post process scheduler state for some operations
    switch(opcode) {
        //Auto increment character position for SPC instructions to simplify ASM
        case vpu::defs::P_BLI_SPC_I:
        case vpu::defs::P_BLI_SPC_R:
            core_blitter_frontend_state.xpos += 8;
            break;
    }


    return true;
}

bool Scheduler::submit_renderer(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2) {
    switch(opcode) {
        case vpu::defs::P_REN_TRN_R:
            core_renderer_frontend_state.transformation_matrix_address = val1;
            return true;
        case vpu::defs::P_REN_STR_R:
            core_renderer_frontend_state.stream_address = val1;
            core_renderer_frontend_state.start_offset = 0;
            core_renderer_frontend_state.end_offset = 0xFFFFFFFF;
            core_renderer_frontend_state.operation = vpu::stream::Operation::RENDER;
            break;
        default: assert(false); //not actually implemented any opcodes yet
    }

    if (renderer_frontend_queue.size() >= vpu::defs::SCHEDULER_FRONTEND_QUEUE_SIZE) {
        return false;
    }

    //When there is space, copy the frontend into the queue
    renderer_frontend_queue.push_back(core_renderer_frontend_state);
    renderer_outstanding++;
    core_renderer_frontend_state.operation = stream::Operation::NONE;

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

void Scheduler::renderer_complete() {
    assert(renderer_outstanding > 0);
    renderer_outstanding--;
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
        case vpu::defs::STREAM_RENDERER:
            return submit_renderer(valid_cycle, opcode, val1, val2);
        default:
            std::cerr << "Scheduler error for opcode " << vpu::defs::opcode_to_string(opcode);
            std::cerr << " No implementation for pipe " << pipe << " ";
            assert(false);
    }
}

void Scheduler::run_cycle() {
    check_pipeline<dma::Command>(dma, core_dma_frontend_state, dma_frontend_queue, std::bind(&Scheduler::dma_complete, this));
    check_pipeline<blit::Command>(blitter, core_blitter_frontend_state, blitter_frontend_queue, std::bind(&Scheduler::blitter_complete, this));
    check_pipeline<stream::Command>(renderer, core_renderer_frontend_state, renderer_frontend_queue, std::bind(&Scheduler::renderer_complete, this));
}

}
