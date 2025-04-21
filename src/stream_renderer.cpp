#include <assert.h>

#include "stream_renderer.h"
#include "blitter.h"

namespace vpu::stream {

ColourTable::ColourTable(std::unique_ptr<vpu::mem::Memory>& memory)
    : memory(memory), memory_return({})
{

}

void ColourTable::initialise(uint32_t addr) {
    cache_valid = true;
    table_addr = addr;
}

void ColourTable::invalidate() {
    presence = {false,false,false,false};
    plru = {false,false,false};
    memory_return_valid = false;
    cache_valid = false;
}

uint8_t ColourTable::update_plru() {
    uint8_t index = 0;

    index |= plru[2] ? 0b10 : 0b00;
    index |= plru[2] ? plru[1] : plru[0];

    uint8_t leaf;
    if (index & 0b10) {
        plru[2] = 0;
        leaf = 1;
    } else {
        plru[2] = 1;
        leaf = 0;
    }

    if (index & 0b01) {
        plru[leaf] = 0;
    } else {
        plru[leaf] = 1;
    }

    return index;
}

bool ColourTable::read_cache(uint32_t index, uint32_t& colour) {
    assert(index < size);
    assert(cache_valid);

    bool present = false;
    uint32_t addr;
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (presence[i] && cache_address[i] == index) {
            present = true;
            addr = i;
        }
    }

    if (!present) {
        uint32_t req_addr = table_addr + (COLOUR_SIZE*index);
        uint32_t req_head = req_addr & ~uint32_t(0x3F);

        //Previous cycle got the data
        if (memory_return_valid && req_head == memory_request_head && memory_return.can_run()) {
            uint8_t evict_way = update_plru();
            cache_data[evict_way].R = memory_return.data[req_addr-req_head+0];
            cache_data[evict_way].G = memory_return.data[req_addr-req_head+1];
            cache_data[evict_way].B = memory_return.data[req_addr-req_head+2];
            cache_address[evict_way] = index;
            presence[evict_way] = true;
            addr = evict_way;
        } else {
            memory_request_head = req_head;
            memory_return_valid = true;
            memory_return = memory->read(memory_request_head);
            return false;
        }
    }

    Colour c = cache_data[addr];
    colour |= c.R << 24;
    colour |= c.G << 16;
    colour |= c.B << 8;
    colour |= 0xFF;

    return true;
}

bool StreamByte::terminal() {
    return (0x80 & data);
}

uint8_t StreamByte::value() {
    return 0x7F & data;
}

uint8_t StreamByte::length_size() {
    return (0x3F & data) + 1;
}

bool StreamByte::length_presence() {
    return 0x40 & data;
}

uint8_t StreamByte::colour_index() {
    return 0x7F & data;
}

Stream::Stream(std::unique_ptr<vpu::mem::Memory>& memory) : colour_table(memory) {}

StreamRenderer::StreamRenderer(std::unique_ptr<vpu::mem::Memory>& memory)
    : memory(memory), memory_return({}), active_stream(memory)
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
            active_stream.colour_table.invalidate();
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
            //Activate the cache for this request. The colour table is located immediately after the header
            active_stream.colour_table.initialise(working_command.stream_address + STREAM_HEADER_BYTES);
            reset_fetch();
            stream_fetch_complete = true;
        }
        return;
    }
        
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
        request_got_bytes < total_bytes && //Stop iteration if we have all the data
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
        case 6: selected_field = &active_stream.colour_table.size; break;
        case 7: selected_field = &active_stream.byte_count; break;
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
    
    //lower always comes first
    if (!upper) {
        transformation.mat[row][col] = byte;
    } else {
        transformation.mat[row][col] |= byte << 8;
    }
}

void StreamRenderer::render_cycle() {
    /*
    This current setup is inefficient, memory reads are only triggered once
    the current data is exhausted, so we have multiple cycles of idle while waiting for data
    

    A higher performance approach would have a double buffer where one is fetched to while
    the other is rendered
    */

    //Done rendering this portion, or the entire stream
    if (processed_byte_count + working_command.start_offset >= working_command.end_offset ||
        processed_byte_count >= active_stream.byte_count) {
        render_state = RenderState::DRAIN;
        active_stream.colour_table.invalidate();
        return;
    }

    if (memory_return_valid == false) {
        render_cycle_data_fetch();
    } else {
        //Data return is defered, no point running a render if there is no data
        render_cycle_process_byte();
    }

}

void StreamRenderer::render_cycle_data_fetch() {
    uint32_t next_stream_byte_addr = working_command.stream_address +
                                     STREAM_HEADER_BYTES +
                                     active_stream.colour_table.size * ColourTable::COLOUR_SIZE +
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
    
    //*** Update internal state with current byte ***//
    if (!process_sequence) { //If processing then the header has already been read
        active_byte = {memory_return.data[internal_buffer_offset]};
        switch (sequence_byte) {
            case 0: //Length value
                byte_voxel_count = active_byte.length_size();
                draw_voxel = active_byte.length_presence();
                break;
            case 1: //Colour value
                colour_index = active_byte.colour_index();
                break;
            default:
                assert(false); //We only have two byte sequences
        }
    }

    //*** Determine action ***/
    bool advance_byte = false;
    if (active_byte.terminal()) {
        process_sequence = true;
    } else {
        advance_byte = true;
    }

    //Read the sequence and state is updated
    if (process_sequence) {
        if (!draw_voxel) { //Empty group
            render_cycle_advance(byte_voxel_count);
            byte_voxel_count = 0; //Can accept any number in the increment
            advance_byte = true;
        } else { //Write voxels of group
            uint32_t voxel_capacity = max_queue_len - output_queue.size();
            uint32_t max_accepted_voxels = std::min(uint32_t(8), std::min(byte_voxel_count, voxel_capacity));
            uint32_t colour;
            bool hit = active_stream.colour_table.read_cache(colour_index, colour);
            if (hit) {
                for (int i = 0; i < max_accepted_voxels; i++) {
                    render_cycle_submit_voxel(colour); //Calculate output of next_to_render
                    render_cycle_advance(1);
                    byte_voxel_count--;
                }

                //completed
                if (byte_voxel_count == 0) advance_byte = true;
            }
        }
    }

    if (advance_byte) {
        //Increment the index, invalidate data once all consumed
        //TODO: overlap request on this cycle, or implement double buffering
        processed_byte_count++;
        internal_buffer_offset++;
        sequence_byte++;
        if (internal_buffer_offset >= vpu::defs::MEM_ACCESS_WIDTH) memory_return_valid = false;

        if (process_sequence) { //reset sequence state
            sequence_byte = 0;
            process_sequence = false;
            assert(byte_voxel_count == 0);
        }
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

void StreamRenderer::render_cycle_submit_voxel(uint32_t colour) {
    if (
        next_to_render.x < active_stream.start.x || next_to_render.x > active_stream.end.x ||
        next_to_render.y < active_stream.start.y || next_to_render.y > active_stream.end.y ||
        next_to_render.z < active_stream.start.z || next_to_render.z > active_stream.end.x
    ) {
        return;
    }
    
    //Make sure we're inside representable range of fixed point format
    assert((next_to_render.x & 0xFFFFF000) == 0);
    assert((next_to_render.y & 0xFFFFF000) == 0);
    assert((next_to_render.z & 0xFFFFF000) == 0);

    //Scale to account for fixed point
    uint16_t x_in = 0xFFFF & (next_to_render.x << 4);
    uint16_t y_in = 0xFFFF & (next_to_render.y << 4);
    uint16_t z_in = 0xFFFF & (next_to_render.z << 4);
    uint16_t w_in = 0xFFFF & (1 << 4); //implicit coordinate
    auto& m = transformation.mat;

    //Apply transformation
    uint32_t x = (m[0][0] * x_in) + (m[1][0] * y_in) + (m[2][0] * z_in) + (m[3][0] * w_in);
    uint32_t y = (m[0][1] * x_in) + (m[1][1] * y_in) + (m[2][1] * z_in) + (m[3][1] * w_in);
    uint32_t z = (m[0][2] * x_in) + (m[1][2] * y_in) + (m[2][2] * z_in) + (m[3][2] * w_in);
    uint32_t w = (m[0][3] * x_in) + (m[1][3] * y_in) + (m[2][3] * z_in) + (m[3][3] * w_in);

    // Scale back to 12.4 fixed point
    x >>= 4;
    y >>= 4;
    z >>= 4;
    w >>= 4;

    //Account for w-scaling, gives scaling factor of 1 as w and x/y/z have same scaling factor
    //This performs both the desired scaling and converting back to integer representation
    x /= w;
    y /= w;
    z /= w;
    w = 0x0001;

    if (x >= vpu::defs::FRAMEBUFFER_WIDTH || y >= vpu::defs::FRAMEBUFFER_HEIGHT) return;
    uint32_t address = vpu::blit::Blitter::pixel_address(x, y);
    output_queue.push_back(Defer<q_entry>({address, colour}));
}

void StreamRenderer::write_queue() {
    if (output_queue.size() == 0) return;
    if (!output_queue.front().can_run()) return;

    auto [address, pixel] = output_queue.front().data;
    output_queue.pop_front();

    memory->write_word(address, pixel);
}

}