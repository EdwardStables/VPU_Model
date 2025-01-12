#include <assert.h>

#include "stream_renderer.h"

namespace vpu::stream {

StreamByte::Type StreamByte::type() {
    return (0x80 & data) ? Type::Length : Type::Mask;
}

uint8_t StreamByte::length() {
    switch(type()){
        case Type::Length: return (0x1F&data);
        case Type::Mask: return 7;
        default: assert(false);
    }
}

StreamRenderer::StreamRenderer(std::unique_ptr<vpu::mem::Memory>& memory)
    : memory(memory), memory_return({})
{

}

bool StreamRenderer::submit(Command command, std::function<void()> completion_callback) {
    assert(command.operation == Operation::RENDER);
    return true;
}

void StreamRenderer::start_fetch() {
    render_state = RenderState::STREAM_FETCH;

    memory_return_valid = false;
    memory_request_head = 0;

    stream_got_bytes = 0;
}

void StreamRenderer::start_render() {
    render_state = RenderState::RENDER;

    memory_return_valid = false;
    memory_request_head = 0;

    processed_byte_count = 0;
    internal_buffer_offset = 0;
    byte_voxel_count = 0;
    next_to_render = active_stream.start;
}

void StreamRenderer::run_cycle() {
    write_queue();

    if (render_state == RenderState::IDLE){
        start_fetch();
    }

    switch(render_state) {
        case RenderState::IDLE: assert(false); break;
        case RenderState::STREAM_FETCH: stream_fetch_cycle(); break;
        case RenderState::RENDER: render_cycle(); break;
    }

    if (render_state == RenderState::IDLE && output_queue.size() == 0){
        state = State::FINISHED;
    }
}

void StreamRenderer::stream_fetch_cycle() {
    //We don't have data, so fetch
    if (memory_return_valid == false) {
        memory_request_head = working_command.stream_address && 0x3F;
        //defer to next cycle
        memory_return = memory->read(working_command.stream_address);
        memory_return_valid = true;
        return;
    }

    //Made the request, but not yet returned, just wait
    if (!memory_return.can_run()) return;

    //At this point we have the stream data
    uint32_t next_header_byte_addr = working_command.stream_address + stream_got_bytes;
    uint32_t request_offset = next_header_byte_addr - memory_request_head;
    
    while (
        stream_got_bytes < stream_header_bytes &&    //Stop iteration if we have all the data
        request_offset < vpu::defs::MEM_ACCESS_WIDTH //Stop iteration if we are crossing cachelines
    ) {
        update_active_stream(stream_got_bytes+1, memory_return.data[request_offset]);
        stream_got_bytes++;
        request_offset += 1;
        request_offset = next_header_byte_addr - memory_request_head;
    }

    if (stream_got_bytes == stream_header_bytes) {
        start_render();
        return;
    }


    //It will be the next cacheline
    memory_request_head += vpu::defs::MEM_ACCESS_WIDTH;
    memory_return = memory->read(memory_request_head);
    memory_return_valid = true;
}

//Write the given byte to the next stream index for the internal datatype
void StreamRenderer::update_active_stream(uint32_t header_index, uint8_t byte) {
    uint32_t* selected_field;
    switch(header_index >> 2) {
        case 0: selected_field = &active_stream.start.x; break;
        case 1: selected_field = &active_stream.start.y; break;
        case 2: selected_field = &active_stream.start.z; break;
        case 3: selected_field = &active_stream.end.x; break;
        case 4: selected_field = &active_stream.end.y; break;
        case 5: selected_field = &active_stream.end.z; break;
        case 6: selected_field = &active_stream.byte_count; break;
        default: assert(false);
    }

    switch(header_index & 0b11) {
        case 0: (*selected_field) = (*selected_field) & 0x00FFFFFF; break;
        case 1: (*selected_field) = (*selected_field) & 0xFF00FFFF; break;
        case 2: (*selected_field) = (*selected_field) & 0xFFFF00FF; break;
        case 3: (*selected_field) = (*selected_field) & 0xFFFFFF00; break;
        default: assert(false);
    }

    switch(header_index & 0b11) {
        case 0: (*selected_field) = (*selected_field) | (byte << 24); break;
        case 1: (*selected_field) = (*selected_field) | (byte << 16); break;
        case 2: (*selected_field) = (*selected_field) | (byte << 8); break;
        case 3: (*selected_field) = (*selected_field) | (byte); break;
        default: assert(false);
    }
}

void StreamRenderer::render_cycle() {
    /*
    This current setup is inefficient, memory reads are only triggered once
    the current data is exhausted, so we have multiple cycles of idle while waiting for data
    

    A higher performance approach would have a double buffer where one is fetched to while
    the other is rendered
    */
    if (memory_return_valid == false) {
        render_cycle_data_fetch();
    } else {
        //Data return is defered, no point running a render if there is no data
        render_cycle_process_byte();
    }

}

void StreamRenderer::render_cycle_data_fetch() {
    //Done rendering this portion
    if (processed_byte_count + working_command.start_offset >= working_command.end_offset) {
        render_state = RenderState::IDLE;
        return;
    }

    uint32_t next_stream_byte_addr = working_command.stream_address +
                                     stream_header_bytes +
                                     working_command.start_offset +
                                     processed_byte_count;

    //We should be cacheline aligned for all transactions excluding the first
    assert(processed_byte_count == 0 || (next_stream_byte_addr & 0x3F == 0));
    //For the initial implementation we only refill once exhausted
    assert(memory_return_valid == false);

    memory_request_head = next_stream_byte_addr && 0x3F;
    memory_return_valid = true;
    internal_buffer_offset = next_stream_byte_addr - memory_request_head;

    //defer to next cycle
    memory_return = memory->read(working_command.stream_address);
}

void StreamRenderer::render_cycle_process_byte() {
    assert(memory_return_valid);
    if (!memory_return.can_run()) return;
    
    //This is starting a new voxel
    if (byte_voxel_count == 0) {
        active_byte = {memory_return.data[internal_buffer_offset+processed_byte_count]};
    }

    bool advance_byte = false;

    //If it's an unset length then just update the next position by the length
    if (active_byte.type() == StreamByte::Type::Length && !(active_byte.data&0x40)) {
        render_cycle_advance(active_byte.length());
        advance_byte = true;
    } else
    //Must be set, we can only accept up to 8 voxels
    if (active_byte.type() == StreamByte::Type::Length) {
        uint32_t remaining_voxels = active_byte.length() - byte_voxel_count;
        uint32_t voxel_capacity = max_queue_len - output_queue.size();
        uint32_t max_accepted_voxels = std::min(uint32_t(8), std::min(remaining_voxels, voxel_capacity));

        for (int i = 0; i < max_accepted_voxels; i++) {
            render_cycle_submit_voxel(); //Calculate output of next_to_render
            render_cycle_advance(1);
            remaining_voxels--;
            byte_voxel_count++;
        }

        //completed
        if (remaining_voxels == 0) advance_byte = true;
    } else {
        assert(active_byte.type() == StreamByte::Type::Mask);

        uint32_t remaining_voxels = active_byte.length() - byte_voxel_count;
        uint32_t voxel_capacity = max_queue_len - output_queue.size();
        uint32_t max_accepted_voxels = std::min(uint32_t(8), std::min(remaining_voxels, voxel_capacity));

        for (int i = 0; i < max_accepted_voxels; i++) {
            if (active_byte.data & (1<<(6-i))) //Check the mask before actually rendering
                render_cycle_submit_voxel();
            render_cycle_advance(1);
            remaining_voxels--;
            byte_voxel_count++;
        }

        //completed
        if (remaining_voxels == 0) advance_byte = true;
    }

    if (advance_byte) {
        //Increment the index, invalidate data once all consumed
        //TODO: overlap request on this cycle, or implement double buffering
        processed_byte_count++;
        byte_voxel_count = 0;
        if (internal_buffer_offset+processed_byte_count >= vpu::defs::MEM_ACCESS_WIDTH) memory_return_valid = false;
    }
}

void StreamRenderer::render_cycle_advance(uint8_t count) {
    for (int i = 0; i < count; i++) {
        uint32_t next_x = next_to_render.x + 1;
        if (next_x <= active_stream.end.x) {
            next_to_render.x = next_x;
            continue;
        }
        next_x = active_stream.start.x;
        uint32_t next_y = next_to_render.y + 1;
        if (next_y <= active_stream.end.y) {
            next_to_render.x = next_x;
            next_to_render.y = next_y;
            continue;
        }
        next_y = active_stream.start.y;
        uint32_t next_z = next_to_render.z + 1;
        if (next_z <= active_stream.end.z) {
            next_to_render.x = next_x;
            next_to_render.y = next_y;
            next_to_render.z = next_z;
            continue;
        }
    }
}

void StreamRenderer::render_cycle_submit_voxel() {
    if (
        next_to_render.x < active_stream.start.x || next_to_render.x > active_stream.end.x ||
        next_to_render.y < active_stream.start.y || next_to_render.y > active_stream.end.y ||
        next_to_render.z < active_stream.start.z || next_to_render.z > active_stream.end.x
    ) {
        return;
    }

    //TODO actually render stuff :kekw:
}

void StreamRenderer::write_queue() {
    if (output_queue.size() == 0) return;
    if (!output_queue.back().can_run()) return;

    auto [address, pixel] = output_queue.back().data;
    output_queue.pop_back();

    //This isn't a practical approach, just enough for getting it to work
    memory->write_word(address+0, 0xFF & (pixel >> 0));
    memory->write_word(address+1, 0xFF & (pixel >> 8));
    memory->write_word(address+2, 0xFF & (pixel >> 16));
    memory->write_word(address+3, 0xFF & (pixel >> 24));
}

}