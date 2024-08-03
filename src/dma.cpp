#include <iostream>
#include <assert.h>

#include "dma.h"

namespace vpu {

DMA::DMA(std::unique_ptr<vpu::mem::Memory>& memory) :
    memory(memory)
{

}

bool DMA::submit(DMA::Command command, std::function<void()> completion_callback) {
    if (state != WORKING){ //Can accept input on the 
        return false;
    }
    assert(command.operation != NONE);
    state = WORKING; 
    work_cycle = vpu::defs::get_next_global_cycle();
    working_command = command;
    working_callback = completion_callback;
    write_pointer = command.dest;
    read_pointer = command.source;
    return false;
}

void DMA::copy_cycle() {
}

void DMA::set_cycle() {
    assert(write_pointer < working_command.dest + working_command.length);
    uint32_t remaining_length = working_command.length - (write_pointer - working_command.dest);
    std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> data;
    data.fill(working_command.value);
    if (remaining_length >= vpu::defs::MEM_ACCESS_WIDTH){
        memory->write(write_pointer, data);
    } else {
        //For now just do single line fetch/respond. Could queue this up properly later if needed
        //Assumes single cycle memory latency and no contention
        if (fetched_data_valid) {
            std::copy(data.begin(),data.begin()+remaining_length, fetched_data.begin());
            memory->write(write_pointer, data);
        } else {
            fetched_data = memory->read(write_pointer);
            fetched_data_valid = true;
            return;
        }
    }
    write_pointer += vpu::defs::MEM_ACCESS_WIDTH;
    if (write_pointer >= working_command.dest + working_command.length){
        state = FINISHED;
    }
}

void DMA::run_cycle() {
    if (state == IDLE) return;
    if (state == FINISHED){ //Finish on the following cycle
        state = IDLE;
        finished_callback();
    }
    
    //Cannot start on first cycle
    if (vpu::defs::get_global_cycle() < work_cycle) return;
    
    switch(working_command.operation){
        case COPY: copy_cycle(); break;
        case SET : set_cycle(); break;
        default:
            std::cerr << "Invalid DMA operation ";
            assert(false);
    }
    
    if (state == FINISHED){
        finished_callback = working_callback;
    }
}

}
