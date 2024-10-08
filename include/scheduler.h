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
    Blitter::Command core_blitter_frontend_state;
    std::deque<Defer<Blitter::Command>> blitter_frontend_queue;


    //Outstanding request count
    uint32_t dma_outstanding = 0;
    void dma_complete();
    uint32_t blitter_outstanding = 0;
    void blitter_complete();

    bool submit_dma(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2);
    bool submit_sched(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2);
    bool submit_blitter(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2);

    void check_dma();
    void check_blitter();
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
