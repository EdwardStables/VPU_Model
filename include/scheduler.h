#pragma once
#include <deque>
#include <tuple>

#include "dma.h"
#include "blitter.h"
#include "defs_pkg.h"
#include "cycle_defer.h"

namespace vpu {

class Scheduler {
    //Pipelines
    DMA& dma;
    Blitter& blitter;

    std::deque<std::tuple<
        uint32_t,    //valid cycle
        defs::Opcode,//operation
        uint32_t,    //operand 1 value
        uint32_t     //operand 2 value
    >> core_input_queue;

    //Frontends maintain state for setting up commands
    //uint32_t is the earliest time it can be submitted
    DMA::Command core_dma_frontend_state;
    std::deque<Defer<DMA::Command>> dma_frontend_queue;
    //Blitter::Frontend blitter_frontend;


    //Outstanding request count
    uint32_t dma_outstanding = 0;
    uint32_t blit_outstanding = 0;

    bool submit_dma(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2);
public:
    Scheduler(
        DMA& dma,
        Blitter& blitter
    );

    //Submit an instruction to the scheduler
    //Returns true if successful, false if there is unsufficient internal buffer space
    //Core is expected to stall if this returns false
    bool core_submit(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2);
    
    //Take the submit instructions and send to appropriate pipeline 
    void run_cycle();
};

}
