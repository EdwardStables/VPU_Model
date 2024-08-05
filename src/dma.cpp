#include <iostream>
#include <assert.h>

#include "dma.h"

namespace vpu {

DMA::DMA(std::unique_ptr<vpu::mem::Memory>& memory) :
    memory(memory)
{

}

bool DMA::submit(DMA::Command command, std::function<void()> completion_callback) {
    if (state == WORKING){ //Can accept input when idle or on last cycle of work
        return false;
    }
    assert(command.operation != NONE);
    assert(command.dest < vpu::defs::MEM_SIZE);
    assert(command.dest + command.length < vpu::defs::MEM_SIZE);

    state = WORKING; 
    work_cycle = vpu::defs::get_next_global_cycle();
    working_command = command;
    working_callback = completion_callback;
    write_pointer = command.dest & 0xFFFFFFC0;
    read_pointer = command.source & 0xFFFFFFC0;
    return true;
}


/*
TODO

Need to carefully check the ranges and access patterns in DMA

Things like read and writes with different alignment will generate a lot of extra traffic

*/

void DMA::copy_cycle() {
    assert(write_pointer < working_command.dest + working_command.length);
    
    //Always read buffer if not containing an entire line, and the read pointer hasn't reached the end
    bool add_to_buffer = active_buffer_size < vpu::defs::MEM_ACCESS_WIDTH && read_pointer < working_command.source + working_command.length;

    if (add_to_buffer){
        std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> fetched_read_data = memory->read(read_pointer);

        //When the return data overlaps the start offset forwards into the buffer
        uint32_t start_offset = working_command.source > read_pointer ? working_command.source - read_pointer : 0;

        //When the return data overlaps the end, offset backwards from the end
        uint32_t end_offset = 
            (read_pointer + vpu::defs::MEM_ACCESS_WIDTH) >= (working_command.source + working_command.length) ?
                vpu::defs::MEM_ACCESS_WIDTH - ((read_pointer + vpu::defs::MEM_ACCESS_WIDTH) - (working_command.source + working_command.length)) :
                vpu::defs::MEM_ACCESS_WIDTH;

        //New entry to active buffer
        if (!active_buffer_valid) {
            active_buffer_valid = true;
            active_buffer_address = working_command.source;
        }
        uint32_t active_offset = active_buffer_size;
        active_buffer_size += end_offset - start_offset;

        std::copy(
            fetched_read_data.begin() + start_offset,
            fetched_read_data.begin() + end_offset,
            active_buffer.begin() + active_offset
        );

        read_pointer += vpu::defs::MEM_ACCESS_WIDTH;
        return;
    }

    //Have done a read and have data
    assert(active_buffer_valid);
    assert(active_buffer_size >= vpu::defs::MEM_ACCESS_WIDTH || read_pointer >= working_command.source + working_command.length);
    assert(active_buffer_address >= working_command.source);
    assert(active_buffer_address + active_buffer_size < working_command.source + working_command.length + vpu::defs::MEM_ACCESS_WIDTH);

    uint32_t start_offset = working_command.dest > write_pointer ? working_command.dest - write_pointer : 0;
    uint32_t end_offset = 
        (write_pointer + vpu::defs::MEM_ACCESS_WIDTH) >= (working_command.dest + working_command.length) ?
            vpu::defs::MEM_ACCESS_WIDTH - ((write_pointer + vpu::defs::MEM_ACCESS_WIDTH) - (working_command.dest + working_command.length)) :
            vpu::defs::MEM_ACCESS_WIDTH;
    
    //Non-standard case, overlaps at one or both end and need to fetch before writing
    bool need_writeback_fetch = !(start_offset == 0 && end_offset == vpu::defs::MEM_ACCESS_WIDTH);

    //Need to first fetch the data and do a selective copy to avoid overwriting outside the range
    if (need_writeback_fetch && !fetched_writeback_data_valid) {
        fetched_writeback_data_valid = true;
        fetched_writeback_data = memory->read(write_pointer);
        return; //Read takes cycle
    }

    uint32_t write_size = end_offset - start_offset;    

    //Either no overlap, therefore fetch buffer overwritten, or overlap and only some data overwritten
    std::copy(
        active_buffer.begin(),
        active_buffer.begin()+write_size,
        fetched_writeback_data.begin()+start_offset
    );

    //Writeback data
    memory->write(write_pointer, fetched_writeback_data);
    //Buffer becomes invalid for new write pointer
    fetched_writeback_data_valid = false;
    //Write pointer always updates by full width
    write_pointer += vpu::defs::MEM_ACCESS_WIDTH;
    

    //Update buffer
    active_buffer_address += write_size;
    active_buffer_size -= write_size;
    std::rotate(active_buffer.begin(), active_buffer.begin()+write_size, active_buffer.end());

    if (write_pointer >= working_command.dest + working_command.length) {
        state = FINISHED;
        active_buffer_valid = false;
        assert(fetched_writeback_data_valid == false);
        assert(active_buffer_size == 0);
        assert(active_buffer_address == working_command.source + working_command.length);
    }
}

void DMA::set_cycle() {
    assert(write_pointer < working_command.dest + working_command.length);
    uint32_t remaining_length = working_command.length - (write_pointer - working_command.dest);
    std::array<uint8_t,vpu::defs::MEM_ACCESS_WIDTH> data;
    data.fill(working_command.value);
    if (remaining_length >= vpu::defs::MEM_ACCESS_WIDTH && write_pointer >= working_command.dest){
        memory->write(write_pointer, data);
    } else {
        //For now just do single line fetch/respond. Could queue this up properly later if needed
        //Assumes single cycle memory latency and no contention

        if (fetched_writeback_data_valid) {
            // Writes and reads must be 64B aligned, therefore need to extend special cases of read, overwrite, writeback
            // to when the range doesn't cover the middle.
            uint32_t start_index = write_pointer >= working_command.dest ? 0 : write_pointer - working_command.dest;
            std::copy(data.begin()+start_index,data.begin()+remaining_length, fetched_writeback_data.begin()+start_index);
            memory->write(write_pointer, data);
            fetched_writeback_data_valid = false;
        } else {
            fetched_writeback_data = memory->read(write_pointer);
            fetched_writeback_data_valid = true;
            return;
        }
    }
    write_pointer += vpu::defs::MEM_ACCESS_WIDTH;
    if (write_pointer >= working_command.dest + working_command.length){
        state = FINISHED;
        assert(fetched_writeback_data_valid == false);
    }
}

void DMA::run_cycle() {
    if (state == IDLE) return;
    if (state == FINISHED){ //Finish on the following cycle
        state = IDLE;
        finished_callback();
        return; //may want to rework this to avoid a bubble
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
