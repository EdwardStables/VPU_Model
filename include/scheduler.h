#pragma once
#include <deque>
#include <tuple>

#include "dma.h"
#include "blitter.h"
#include "stream_renderer.h"
#include "defs_pkg.h"
#include "cycle_defer.h"

namespace vpu {

class Scheduler {
    //Pipelines
    dma::DMA& dma;
    blit::Blitter& blitter;
    stream::StreamRenderer& renderer;

    std::deque<std::tuple<
        uint32_t,    //valid cycle
        defs::Opcode,//operation
        uint32_t,    //operand 1 value
        uint32_t     //operand 2 value
    >> core_input_queue;

    //Frontends maintain state for setting up commands
    //uint32_t is the earliest time it can be submitted
    dma::Command core_dma_frontend_state;
    std::deque<Defer<dma::Command>> dma_frontend_queue;
    blit::Command core_blitter_frontend_state;
    std::deque<Defer<blit::Command>> blitter_frontend_queue;
    stream::Command core_renderer_frontend_state;
    std::deque<Defer<stream::Command>> renderer_frontend_queue;

    //Outstanding request count
    uint32_t dma_outstanding = 0;
    void dma_complete();
    uint32_t blitter_outstanding = 0;
    void blitter_complete();
    uint32_t renderer_outstanding = 0;
    void renderer_complete();

    bool submit_sched(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2);
    bool submit_dma(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2);
    bool submit_blitter(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2);
    bool submit_renderer(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2);

    template <class C>
    void check_pipeline(Subsystem<C>& subsystem, C& command, std::deque<Defer<C>>& queue, std::function<void()> callback);
public:
    Scheduler(
        dma::DMA& dma,
        blit::Blitter& blitter,
        stream::StreamRenderer& renderer
    );

    //Submit an instruction to the scheduler
    //Returns true if successful, false if there is unsufficient internal buffer space
    //Core is expected to stall if this returns false
    bool core_submit(uint32_t valid_cycle, defs::Opcode opcode, uint32_t val1, uint32_t val2);
    
    //Take the submit instructions and send to appropriate pipeline 
    void run_cycle();
};

template <class C>
void Scheduler::check_pipeline(
    Subsystem<C>& subsystem,
    C& command,
    std::deque<Defer<C>>& queue,
    std::function<void()> callback
){
    //Nothing there
    if (!queue.size()) return;
    //Can't run yet
    if (!queue.front().can_run()) return;

    //Can accept data
    if (subsystem.base_submit(queue.front().data, callback)){
        queue.pop_front();
        return;
    }

    //Otherwise it couldn't accept, need to increment all the valid cycles in the queue
    for (auto& cmd : queue){
        cmd.increment();
    }
}

}
