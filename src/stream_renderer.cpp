#include <assert.h>

#include "stream_renderer.h"
#include "blitter.h"

namespace vpu::stream {

StreamByte::Type StreamByte::type() {
    return (0x80 & data) ? Type::Length : Type::Mask;
}

uint8_t StreamByte::length() {
    switch(type()){
        case Type::Length: return (0x3F&data)+1;
        case Type::Mask: return 7;
        default: assert(false);
    }
}

StreamRenderer::StreamRenderer(std::unique_ptr<vpu::mem::Memory>& memory)
    : memory(memory), memory_return({})
{

}

bool StreamRenderer::submit() {
    assert(working_command.operation == Operation::RENDER);
    return true;
}

void StreamRenderer::start_fetch() {
    render_state = RenderState::DATA_FETCH;

    transformation.valid = false;
    stream_fetch_complete = false;

    reset_fetch();
}

void StreamRenderer::reset_fetch() {
    memory_return_valid = false;
    memory_request_head = 0;
    request_got_bytes = 0;
}

void StreamRenderer::start_render() {
    render_state = RenderState::RENDER;

    processed_byte_count = 0;
    internal_buffer_offset = 0;
    byte_voxel_count = 0;
    next_to_render = active_stream.start;

    reset_fetch();
}

void StreamRenderer::run_cycle() {
    write_queue();

    //Don't want to switch to a fetch again if we have things to write out
    //Creates a bubble, but that's ok
    if (render_state == RenderState::DRAIN) {
        if (output_queue.size() == 0){
            state = State::FINISHED;
            render_state = RenderState::IDLE;
        }
        return;
    }

    if (render_state == RenderState::IDLE){
        start_fetch();
    }

    switch(render_state) {
        case RenderState::IDLE: assert(false); break;
        case RenderState::DATA_FETCH: data_fetch_cycle(); break;
        case RenderState::RENDER: render_cycle(); break;
    }

}

//Runs the operations serially, good enough for current modeling requirements
void StreamRenderer::data_fetch_cycle() {
    if (!stream_fetch_complete) {
        data_fetch_cycle_request(working_command.stream_address, STREAM_HEADER_BYTES, &StreamRenderer::update_active_stream);
        if (request_got_bytes == STREAM_HEADER_BYTES) {
            reset_fetch();
            stream_fetch_complete = true;
        }
        return;
    }
        
    if (false) //don't run this for now
    if (!transformation.valid) {
        data_fetch_cycle_request(working_command.transformation_matrix_address, TRANSFORM_BYTES, &StreamRenderer::update_transform);
        if (request_got_bytes == TRANSFORM_BYTES) {
            reset_fetch();
            transformation.valid = true;
        }
        return;
    }

    start_render();
}

void StreamRenderer::data_fetch_cycle_request(const uint32_t base_address, const uint32_t total_bytes, update_callback update) {
    //We don't have data, so fetch
    if (memory_return_valid == false) {
        memory_request_head = base_address & ~uint32_t(0x3F);
        //defer to next cycle
        memory_return = memory->read(memory_request_head);
        memory_return_valid = true;
        return;
    }

    //Made the request, but not yet returned, just wait
    if (!memory_return.can_run()) return;

    //At this point we have the stream data
    uint32_t next_header_byte_addr = base_address + request_got_bytes;
    uint32_t request_offset = next_header_byte_addr - memory_request_head;
    
    while (
        request_got_bytes < STREAM_HEADER_BYTES &&    //Stop iteration if we have all the data
        request_offset < vpu::defs::MEM_ACCESS_WIDTH //Stop iteration if we are crossing cachelines
    ) {
        update(this, request_got_bytes, memory_return.data[request_offset]);
        request_got_bytes++;
        request_offset += 1;
    }

    if (request_got_bytes == total_bytes) {
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

void StreamRenderer::update_transform(uint32_t byte_index, uint8_t byte) {
    bool upper = 0x1 & byte_index;
    uint32_t index = byte_index / 2;
    uint32_t row = index / 4;
    uint32_t col = index % 4;
    uint16_t v = transformation.mat[row][col];
    uint16_t nv = byte;
    v |= upper ? 0xFF00 : 0x00FF;
    v &= upper ? nv << 8 : nv;
    transformation.mat[row][col] = v;
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
    //Done rendering this portion, or the entire stream
    if (processed_byte_count + working_command.start_offset >= working_command.end_offset ||
        processed_byte_count >= active_stream.byte_count) {
        render_state = RenderState::DRAIN;
        return;
    }

    uint32_t next_stream_byte_addr = working_command.stream_address +
                                     STREAM_HEADER_BYTES +
                                     working_command.start_offset +
                                     processed_byte_count;

    //We should be cacheline aligned for all transactions excluding the first
    assert(processed_byte_count == 0 || (next_stream_byte_addr & 0x3F) == 0);
    //For the initial implementation we only refill once exhausted
    assert(memory_return_valid == false);

    memory_request_head = next_stream_byte_addr & ~uint32_t(0x3F);
    memory_return_valid = true;
    internal_buffer_offset = next_stream_byte_addr - memory_request_head;

    //defer to next cycle
    memory_return = memory->read(memory_request_head);
}

void StreamRenderer::render_cycle_process_byte() {
    assert(memory_return_valid);
    if (!memory_return.can_run()) return;
    
    //This is starting a new voxel
    if (byte_voxel_count == 0) {
        active_byte = {memory_return.data[internal_buffer_offset]};
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
        internal_buffer_offset++;
        byte_voxel_count = 0;
        if (internal_buffer_offset >= vpu::defs::MEM_ACCESS_WIDTH) memory_return_valid = false;
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

        //Gotten to the end of z, must be done
        render_state = RenderState::DRAIN;
        break;
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



    uint32_t address = vpu::blit::Blitter::pixel_address(next_to_render.x, 60-next_to_render.z);
    output_queue.push_back(Defer<q_entry>({address, 0xFFFFFFFF}));
}

void StreamRenderer::write_queue() {
    if (output_queue.size() == 0) return;
    if (!output_queue.front().can_run()) return;

    auto [address, pixel] = output_queue.front().data;
    output_queue.pop_front();

    memory->write_word(address, pixel);
}

}